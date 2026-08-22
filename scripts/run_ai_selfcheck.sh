#!/bin/bash
# Build and run the JUCE-independent AI harness checks.
# The check intentionally avoids the full application build.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

clang++ -std=c++17 -fsanitize=address,undefined -g -Wall -Wextra \
    -I "$ROOT/Projucer/Source/AI" \
    "$ROOT/Projucer/Source/AI/Tests/ai_selfcheck.cpp" \
    "$ROOT/Projucer/Source/AI/jucer_SseParser.cpp" \
    "$ROOT/Projucer/Source/AI/jucer_AiPaths.cpp" \
    -o "$OUT/ai_selfcheck"

"$OUT/ai_selfcheck"
