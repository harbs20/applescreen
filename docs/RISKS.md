# Risks

Ordered by how likely each is to actually block v1. Each entry says what's
been verified so far and what still needs a real Minecraft/Prism Launcher
test - this repo's automated build has no way to launch a real JVM+LWJGL
process, so several of these are still open until you run them by hand.

## Verified during implementation (fixed, not just theoretical)

These three were caught empirically while building v1, on this specific
machine/toolchain (Apple Silicon, macOS Sequoia-class, Xcode 21 clang) -
they weren't in the original risk list because they're toolchain/platform
quirks rather than JVM-signing risks, but they would have silently broken
the whole mechanism if left unfixed:

1. **The linker moves `__DATA,__interpose` into `__DATA_CONST`.** The modern
   linker's read-only-data migration pass relocates a `const`-qualified (and,
   it turns out, even non-const) interpose struct out of `__DATA` and into
   `__DATA_CONST` despite the explicit `section("__DATA,__interpose")`
   attribute, which is not where every real-world interpose example expects
   it and not worth gambling dyld's interpose scanner finding it there.
   **Fix**: link both dylib targets with `-Wl,-ld_classic` (see
   `core/CMakeLists.txt`); confirmed via `otool -l` that the section lands
   in plain `__DATA` with that flag, `__DATA_CONST` without it. `ld_classic`
   is marked deprecated by Apple and may eventually be removed - if a future
   Xcode drops it, this needs a different fix (possibly a linker script, or
   by then dyld may also scan `__DATA_CONST` for interpose sections).

2. **Bootstrapping "the real dlsym" via `dlsym(RTLD_NEXT, "dlsym")`
   recurses infinitely.** Any literal call to the symbol `dlsym` anywhere in
   the interposing dylib - including one meant to look up the *real*
   implementation - is itself rewritten by the same process-wide interpose
   table, so it calls back into `hooked_dlsym`, which tries to bootstrap
   again, forever. Verified by reproducing it: an earlier version of this
   code, injected into a trivial test harness, produced ~130,000 log lines
   in a few seconds before something (likely a stack-overflow-triggered
   crash-reporting path) tore the process down. **Fix**: never call `dlsym`
   textually to find the real implementation. The interpose struct's
   `replacee` field is a plain data relocation to the real address, resolved
   before any rewriting is applied - read that field back directly instead
   (see the comment in `core/src/interpose.c`).

3. **`AF_UNIX` + `SOCK_SEQPACKET` isn't supported on macOS.**
   `socket(AF_UNIX, SOCK_SEQPACKET, 0)` returns `EPROTONOSUPPORT` even though
   the constant compiles fine - macOS's implementation only supports
   `SOCK_STREAM`/`SOCK_DGRAM` for `AF_UNIX`, unlike Linux. The original design
   assumed SEQPACKET's atomic per-message delivery to avoid needing framing.
   **Fix**: `SOCK_STREAM` with a fixed-size read/write loop (`recv_full`/
   `send_full`), since every message is a constant-size POD anyway - no
   length-prefix framing needed.

What's been verified with these fixes in place, end to end, against a
throwaway local test harness (not real Minecraft): injection loads, the
`dlsym` interpose observes and can override GLFW symbol names, the
on-demand fallback resolution works, the command queue drains correctly
from a simulated per-frame `glfwPollEvents` loop, the full IPC round trip
works from both a C test client and the actual Swift `IPCClient`/
`ipc-smoke-test` tool, and `PING`/`SET_WINDOW_POS` return the expected
status codes (including the "no window yet" guard).

## Still open - need a real Minecraft/Prism Launcher test

4. **Hardened Runtime may silently strip `DYLD_INSERT_LIBRARIES`.** If the
   launcher's bundled `java` is Hardened-Runtime-signed without
   `com.apple.security.cs.allow-dyld-environment-variables`, the env var is
   stripped before `exec` with no error - Minecraft just launches unhooked.
   Detect: `codesign -d --verbose=4` / `--entitlements :-` on the java
   binary (automated in `InjectionManager.inspectSigning`). Mitigate:
   `InjectionManager.prepareJavaForInjection` makes a local, ad-hoc-resigned
   copy with the needed entitlements in Application Support - redo whenever
   the launcher updates its bundled JRE.

5. **Library Validation may block the dylib's own `dlopen`.** Even with #4
   fixed, if the java binary's signature doesn't match ours, loading our
   dylib can fail outright (loud failure - crash/codesign error in
   Console.app). Same mitigation as #4, adding
   `com.apple.security.cs.disable-library-validation`.

6. **Does `dlsym` interpose actually catch `glfwCreateWindow` for the
   specific LWJGL/JDK build a real launcher provisions?** Verified for a
   trivial harness calling `dlsym(RTLD_DEFAULT, "glfwCreateWindow")`
   directly; NOT yet verified for LWJGL's actual native-library loading
   path, which is what M0 (`scripts/inject_spike.sh` + a real Prism Launcher
   instance) is for. If LWJGL statically links instead of resolving via
   `dlsym` for some symbols, `window_control.c`'s on-demand fallback
   (resolving against the cached glfw handle) should still work as long as
   *at least one* glfw symbol is resolved via dlsym first to capture that
   handle - but this is unverified against the real thing.

Low risk / dismissed: SIP (doesn't protect third-party JDKs in
user-writable dirs), Gatekeeper/quarantine (irrelevant for a locally-built
dylib injected via env var rather than downloaded/double-clicked - revisit
when this becomes a distributed download), setuid (java isn't setuid),
Apple Silicon ad-hoc signing requirement (not really a risk - a mandatory,
automated CMake post-build step, see `core/CMakeLists.txt`).

## Next step

Run `scripts/build_core.sh`, then set
`DYLD_INSERT_LIBRARIES=$(pwd)/build/core/libapplescreen_spike.dylib` as a
per-instance environment variable in a real Prism Launcher/MultiMC instance,
launch Minecraft, and check `grep -i glfw /tmp/applescreen_spike.log`. That
single test resolves risks #4-#6 before investing further in v1's remaining
milestones (M1's window-move sanity check, M5 hardening, M6 docs already
mostly written here).
