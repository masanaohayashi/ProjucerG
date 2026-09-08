#!/bin/bash
# Build and run the AI harness checks.
# The check intentionally avoids the full application build. Everything except
# the session store is JUCE-independent; the store needs juce_core only.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

JUCE_MODULES="$ROOT/OnDeviceBuild/dependencies/JUCE/modules"

# juce_core is compiled once here so the check keeps working without an IDE
# project. The GUI modules are deliberately left out.
clang++ -std=c++17 -x objective-c++ -O0 -g -w \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DNDEBUG=1 \
    -I "$JUCE_MODULES" \
    -c "$JUCE_MODULES/juce_core/juce_core.mm" \
    -o "$OUT/juce_core.o"

clang -O0 -g -w \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -I "$JUCE_MODULES" \
    -c "$JUCE_MODULES/juce_core/juce_core_zlib.c" \
    -o "$OUT/juce_core_zlib.o"

clang++ -std=c++17 -O0 -g -w \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -I "$JUCE_MODULES" \
    -c "$JUCE_MODULES/juce_core/juce_core_CompilationTime.cpp" \
    -o "$OUT/juce_core_time.o"

clang++ -std=c++17 -fsanitize=address,undefined -g -Wall -Wextra \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DNDEBUG=1 \
    -I "$ROOT/Projucer/Source/AI" \
    -I "$JUCE_MODULES" \
    "$ROOT/Projucer/Source/AI/Tests/ai_selfcheck.cpp" \
    "$ROOT/Projucer/Source/AI/jucer_SseParser.cpp" \
    "$ROOT/Projucer/Source/AI/jucer_AiPaths.cpp" \
    "$ROOT/Projucer/Source/AI/jucer_AiSessionStore.cpp" \
    "$OUT/juce_core.o" "$OUT/juce_core_time.o" "$OUT/juce_core_zlib.o" \
    -framework Cocoa -framework Carbon -framework IOKit -framework Security \
    -o "$OUT/ai_selfcheck"

"$OUT/ai_selfcheck"
