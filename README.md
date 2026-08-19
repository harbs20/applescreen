# Applescreen

A macOS injection-based speedrunning tool for Minecraft, built as a shadow
of [tuxinjector](https://github.com/flammablebunny/tuxinjector),
[waywall](https://github.com/tesselslate/waywall), and
[Toolscreen](https://github.com/jojoe77777/Toolscreen).

**Status: v1 (injection core + single-instance window control) implemented,
locally verified against a synthetic test harness, not yet verified against
a real Minecraft install.** See [docs/RISKS.md](docs/RISKS.md) for exactly
what's confirmed vs. still open, and run the M0 spike there before trusting
this against a real game.

## What's here

- `core/` - the injected C dylib. `DYLD_INSERT_LIBRARIES` + `dlsym`
  interposition to hook into a running Minecraft/LWJGL process and control
  its GLFW window (move/resize/focus/query) over local IPC.
- `app/` - a Swift/AppKit control app (SwiftPM package) that launches
  Minecraft with the injection wired up and drives the window controls.
- `docs/ARCHITECTURE.md` - how it works and the repo layout.
- `docs/RISKS.md` - what's verified, what's still open, ordered by how
  likely each is to block things.

## Quickstart

```bash
scripts/build_core.sh     # build + ad-hoc sign the core and M0 spike dylibs
cd app && swift build     # build the control app and the ipc-smoke-test CLI
```

Run the app: `app/.build/debug/Applescreen`.

Before pointing this at a real Minecraft instance, run the M0 spike per
`docs/RISKS.md` against a real Prism Launcher/MultiMC instance - none of
the JVM/launcher-signing risks there have been verified on a real install
yet.
