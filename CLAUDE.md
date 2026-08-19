# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Applescreen is a macOS injection-based Minecraft speedrunning tool, built
as a shadow of three real community tools: **tuxinjector** (proves the
macOS `dlsym`-interpose mechanism this repo uses), **waywall** (window/wall
management, Wayland-specific, informs later phases), and **Toolscreen**
(Windows DLL injection, overlays/modes, informs later phases). v1's scope
is deliberately narrow: inject into a running Minecraft/LWJGL process on
macOS and control its GLFW window (move/resize/focus/query) from an
external Swift app over local IPC. No overlay rendering, wall, HUD, input
rebinding, or Lua yet.

Two independent build systems live side by side: `core/` is a CMake-built C
dylib injected into Minecraft's JVM process; `app/` is a Swift Package
Manager AppKit control app. There is no shared build step - build each
independently (or via the scripts below).

## Commands

```bash
scripts/build_core.sh [Debug|Release]   # build + ad-hoc sign build/core/lib{applescreen_core,applescreen_spike}.dylib
cd app && swift build                    # build the control app + ipc-smoke-test CLI (debug)
cd app && swift build -c release         # release build
scripts/package_app.sh                   # release build of both + assemble dist/Applescreen.app and dist/Applescreen.dmg
```

Run things:
- Control app: `app/.build/debug/Applescreen`
- Standalone IPC test client (no GUI): `app/.build/debug/ipc-smoke-test <command-type-int>` -
  see `applescreen_cmd_type_t` in `core/include/applescreen/ipc_protocol.h` for the int values
  (1=PING, 2=SET_WINDOW_POS, 3=SET_WINDOW_SIZE, 4=FOCUS_WINDOW, 5=GET_WINDOW_POS, 6=GET_WINDOW_SIZE, 7=SET_WINDOW_SHOULD_CLOSE)
- M0 injection spike (observe-only, no overrides): `scripts/inject_spike.sh <command>` for a
  local sanity check, or set `DYLD_INSERT_LIBRARIES` to the built `libapplescreen_spike.dylib`
  path as a per-instance environment variable in Prism Launcher/MultiMC for the real test, then
  `grep -i glfw /tmp/applescreen_spike.log`

There is no automated test suite (no ctest target, no XCTest target) - the
only end-to-end verification path today is manual: inject into a real
Minecraft instance, or simulate one with a throwaway harness that calls
`dlsym` for GLFW symbol names and loops calling the resolved
`glfwPollEvents` (see the "Full end-to-end IPC round trip" testing done
during initial development for the pattern - a trivial C program that
`dlsym`s `glfwPollEvents` and calls it in a loop is enough to drive the
command queue's drain point without needing real GLFW/Minecraft present).

Logs from the injected dylib land in `/tmp/applescreen_core.log` (or
`/tmp/applescreen_spike.log` for the M0 spike) - injected code has no
attached console, so this is the only way to observe it.

## Architecture

**Cross-language wire format.** `core/include/applescreen/ipc_protocol.h`
is the single canonical definition of the IPC command/response structs
(fixed-width types only, `_Static_assert` size tripwires). The Swift side
does not hand-mirror these structs - `app/Sources/CIPCProtocol/` is a thin
C target whose umbrella header just `#include`s the real header via a
relative path, so `import CIPCProtocol` in Swift gets the exact same
layout. If you add a field, there is exactly one place to edit it.

**Injection mechanism** (`core/src/interpose.c`). Loaded via
`DYLD_INSERT_LIBRARIES`, the dylib installs a `__DATA,__interpose` entry
for `dlsym` itself, because LWJGL resolves its GLFW/GL native functions at
runtime via `dlsym` rather than static linking. Every symbol name the
process resolves flows through the hook; only `glfwCreateWindow`,
`glfwDestroyWindow`, and `glfwPollEvents` are actually overridden (see
`core/src/glfw_shim.c`). Once any `glfw*` symbol has been observed, the
dlopen handle used to resolve it is cached, so any other GLFW function can
be resolved on demand against that same handle later even if the game
itself never dlsym's that particular name (`window_control.c`'s
`resolve_cached()`).

**Two non-obvious build/runtime requirements that must not be "cleaned
up"** - both were found empirically and would silently break injection if
reverted:
- Both dylib targets link with `-Wl,-ld_classic` (see `core/CMakeLists.txt`).
  Without it, the modern linker relocates the `__interpose` section into
  `__DATA_CONST` instead of plain `__DATA` despite the explicit section
  attribute, and `-ld_classic` is the only known fix; it is deprecated but
  still required. Verify with `otool -l <dylib> | grep -A3 __interpose` -
  `segname` must read `__DATA`.
- Never call `dlsym(...)` textually anywhere in `interpose.c`/`spike.c` to
  bootstrap "the real dlsym" (e.g. via `dlsym(RTLD_NEXT, "dlsym")`) - that
  call is itself rewritten by the same interpose table and recurses
  infinitely. The real implementation must come from reading the interpose
  struct's `replacee` field directly (a plain data relocation, resolved
  before any rewriting is applied).

**IPC transport is `AF_UNIX` + `SOCK_STREAM`, not `SOCK_SEQPACKET`** -
`SOCK_SEQPACKET` is not supported for `AF_UNIX` on macOS
(`EPROTONOSUPPORT`), unlike Linux. Since every message is a fixed-size POD,
both sides just loop `send`/`recv` until exactly `sizeof(...)` bytes have
moved (`recv_full`/`send_full` in `core/src/ipc_server.c`, the equivalent
loop in `IPCClient.swift`) - no length-prefix framing needed.

**Threading.** GLFW's Cocoa backend requires window calls on the main
thread. The IPC server runs on background pthreads (accept loop + one
thread per connection). Commands go into a mutex-protected ring buffer
(`core/src/command_queue.c`); the submitting thread blocks on a per-request
condvar. The main thread drains the queue once per frame from inside the
wrapped `glfwPollEvents` (already called every frame, so no separate hook
point is needed), dispatches against the real GLFW pointers
(`window_control.c`), and signals the waiting connection thread with the
result.

**Signing.** Ad-hoc code signing of both dylib targets happens
automatically as a CMake post-build step - mandatory on Apple Silicon, not
optional (an unsigned dylib won't load at all, even injected ones), so
there is deliberately no separate manual signing script to forget to run.
`InjectionManager.swift` (Swift app) separately handles the *java binary's*
signing: if it's Hardened-Runtime-signed without
`com.apple.security.cs.allow-dyld-environment-variables` /
`com.apple.security.cs.disable-library-validation`, `DYLD_INSERT_LIBRARIES`
gets silently stripped or blocked - `prepareJavaForInjection` makes a
local, re-signed copy in Application Support rather than modifying the
launcher-provisioned original.

**What's verified vs. still open** lives in `docs/RISKS.md` - read it
before assuming the injection mechanism works against a specific real
Minecraft/launcher setup; several risks there are toolchain-verified
locally but not yet confirmed against a real JVM/LWJGL process.
