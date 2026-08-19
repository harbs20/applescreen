#!/bin/sh
# Configures and builds the injected core (and the M0 spike dylib) via
# CMake. Ad-hoc code-signing happens automatically as a post-build step in
# core/CMakeLists.txt - it's mandatory on Apple Silicon, not optional, so
# it's not a separate script anyone could forget to run.
set -e
cd "$(dirname "$0")/.."
cmake -S . -B build -DCMAKE_BUILD_TYPE="${1:-Debug}"
cmake --build build
echo "Built:"
echo "  build/core/libapplescreen_spike.dylib  (M0 - observe-only, see docs/RISKS.md)"
echo "  build/core/libapplescreen_core.dylib   (real injection core)"
