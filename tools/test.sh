#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror \
  "$ROOT/tests/test_ambient_scope.cpp" \
  "$ROOT/firmware/AmbientScope/src/AudioAnalyzer.cpp" \
  "$ROOT/firmware/AmbientScope/src/AmbientScopeRenderer.cpp" \
  -o "$ROOT/build/test-ambient-scope"
"$ROOT/build/test-ambient-scope"
