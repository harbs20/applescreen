# Applescreen v1 Architecture

Applescreen is a macOS injection-based Minecraft speedrunning tool, built as
a shadow of three real community tools:

- **[tuxinjector](https://github.com/flammablebunny/tuxinjector)** - proves
  the macOS mechanism this project uses: `DYLD_INSERT_LIBRARIES` + interposing
  `dlsym` itself, because LWJGL resolves its GLFW/GL native functions at
  runtime via `dlsym` rather than static linking.
- **[waywall](https://github.com/tesselslate/waywall)** - window/instance
  management and wall/reset workflow, Wayland-specific; informs later phases.
- **[Toolscreen](https://github.com/jojoe77777/Toolscreen)** - Windows DLL
  injection, screen mirroring, mode presets, hotkeys; informs later phases.

v1's scope is deliberately narrow: prove (a) our C code can run inside a
live Minecraft/JVM process on macOS via injection, and (b) we can control
the real GLFW window (move/resize/focus/query) from an external Swift app
over local IPC. No overlay rendering, no multi-instance wall, no HUD, no
input rebinding, no Lua - those are later phases.

## How injection works

`DYLD_INSERT_LIBRARIES=/path/to/libapplescreen_core.dylib` is set as a
per-instance environment variable in the launcher (Prism Launcher/MultiMC -
the vanilla launcher has no per-instance env var UI and isn't supported).
On load, the dylib installs a `__DATA,__interpose` entry for `dlsym` itself.
Every symbol name the JVM/LWJGL resolves at runtime flows through the
wrapper in `core/src/interpose.c`, which passes almost everything straight
through except for `glfwCreateWindow`, `glfwDestroyWindow`, and
`glfwPollEvents`, which get overridden.

Once `glfwCreateWindow` has been observed, the dlopen handle used to
resolve it is cached (`applescreen_glfw_handle()`), so any other GLFW
function (`glfwSetWindowPos`, `glfwFocusWindow`, etc.) can be resolved
on-demand against that same handle later - even if the game itself never
happens to dlsym that particular name. This is what `window_control.c`'s
`resolve_cached()` does.

## Threading

GLFW's Cocoa backend requires window calls on the main thread. The IPC
server runs on background pthreads (one accept loop, one per connection).
Incoming commands are pushed onto a mutex-protected ring buffer
(`command_queue.c`) and the submitting thread blocks on a per-request
condvar. The main thread drains that queue once per frame, from inside the
wrapped `glfwPollEvents` (called every frame already, so no separate hook
point is needed), dispatches each command against the real GLFW pointers,
and signals the waiting connection thread with the result.

## IPC

One canonical C header, `core/include/applescreen/ipc_protocol.h`, defines
fixed-size POD command/response structs. It's wrapped as a tiny
Swift-importable C module at `app/Sources/CIPCProtocol/` so the Swift app
imports the *exact same* struct layout via `import CIPCProtocol` - zero
hand-mirrored drift.

Transport is `AF_UNIX` + `SOCK_STREAM` at `/tmp/applescreen.sock`.
**`SOCK_SEQPACKET` was the original plan but does not work on macOS** -
`socket(AF_UNIX, SOCK_SEQPACKET, 0)` returns `EPROTONOSUPPORT` (verified
empirically; macOS's `AF_UNIX` implementation only supports `SOCK_STREAM`
and `SOCK_DGRAM`, unlike Linux). Since every message is a fixed-size POD,
both sides just loop `send`/`recv` until exactly `sizeof(...)` bytes have
moved - see `recv_full`/`send_full` in `ipc_server.c` and the equivalent in
`IPCClient.swift`.

## Repo layout

```
applescreen/
  CMakeLists.txt
  core/                                    # C injected dylib
    CMakeLists.txt
    include/applescreen/ipc_protocol.h
    src/
      init.c            # constructor: starts ipc_server (interpose installs itself via the linker)
      interpose.c/.h      # __DATA,__interpose table + dlsym wrapper + glfw handle cache
      glfw_shim.c/.h        # glfwCreateWindow/glfwDestroyWindow/glfwPollEvents overrides
      command_queue.c/.h     # ring buffer + per-request condvar handoff
      window_control.c/.h     # Command -> real GLFW call dispatch
      ipc_server.c/.h           # AF_UNIX/SOCK_STREAM server, background pthreads
      log.c/.h                    # file logger (injected code has no console)
    spike/spike.c                  # M0: observe-only dlsym logger, no overrides
  app/                                     # Swift/AppKit control app (SwiftPM package)
    Package.swift
    Sources/
      CIPCProtocol/            # C target wrapping core/include/.../ipc_protocol.h for Swift
      Applescreen/              # the AppKit app
        main.swift / AppDelegate.swift
        Controllers/InjectionManager.swift        # java signing checks + injection launch
        Controllers/WindowControlViewModel.swift   # UI <-> IPCClient/InjectionManager glue
        IPC/IPCClient.swift                          # AF_UNIX/SOCK_STREAM client
        Views/MainViewController.swift / ClosureButton.swift
      ipc-smoke-test/            # standalone CLI for exercising the IPC layer without the GUI
  scripts/
    build_core.sh
    inject_spike.sh
  docs/
    ARCHITECTURE.md (this file)
    RISKS.md
```

Ad-hoc code signing of the dylibs happens automatically as a CMake
post-build step (mandatory on Apple Silicon - an unsigned dylib won't load
at all, even injected ones) - there's no separate manual signing script to
forget to run.

## Building and running

```bash
scripts/build_core.sh          # builds + signs build/core/lib{applescreen_core,applescreen_spike}.dylib
cd app && swift build          # builds the control app + smoke-test CLI
```

Run the control app: `app/.build/debug/Applescreen`.
Run the standalone IPC smoke test against an already-injected process:
`app/.build/debug/ipc-smoke-test <command-type-int>` (1=PING, 2=SET_WINDOW_POS, ...
see `applescreen_cmd_type_t` in `ipc_protocol.h`).

**Before trusting any of this against real Minecraft**, run the M0 spike
per `docs/RISKS.md` - none of the JVM/launcher-specific risks there have
been verified on a real Minecraft install yet, only the injection mechanism
itself (against a trivial local test harness).
