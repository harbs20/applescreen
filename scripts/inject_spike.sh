#!/bin/sh
# M0 spike test runner. Injects the observe-only spike dylib into whatever
# command you pass and reports where the log landed. For the real test
# against Minecraft, don't use this script directly - instead set
# DYLD_INSERT_LIBRARIES as a per-instance environment variable in Prism
# Launcher/MultiMC (see docs/ARCHITECTURE.md) so it applies to the actual
# java process the launcher spawns, then run this script's grep step
# against the resulting log by hand.
set -e
cd "$(dirname "$0")/.."
DYLIB="build/core/libapplescreen_spike.dylib"

if [ ! -f "$DYLIB" ]; then
    echo "Build it first: scripts/build_core.sh" >&2
    exit 1
fi

rm -f /tmp/applescreen_spike.log

if [ "$#" -gt 0 ]; then
    echo "Injecting into: $*"
    DYLD_INSERT_LIBRARIES="$(pwd)/$DYLIB" "$@"
else
    echo "No command given - just confirming the dylib loads standalone won't tell you much."
    echo "Usage: $0 <command to run with the spike injected>"
    echo "For the real test, set DYLD_INSERT_LIBRARIES in your launcher's instance settings instead."
fi

echo
echo "Log: /tmp/applescreen_spike.log"
echo "Check for injection + glfw symbol resolution:"
echo "  grep -i glfw /tmp/applescreen_spike.log"
