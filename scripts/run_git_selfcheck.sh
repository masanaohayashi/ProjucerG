#!/bin/bash
# Build and run the built-in git checks against a throwaway repository.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JUCE="$ROOT/OnDeviceBuild/dependencies/JUCE"
LIBGIT2="$ROOT/OnDeviceBuild/third_party/libgit2"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

if [ ! -f "$LIBGIT2/build-macos/libgit2.a" ]; then
    echo "libgit2 for macOS is missing. Run scripts/build_libgit2.sh first." >&2
    exit 1
fi

clang++ -std=c++17 -g -Wall -Wextra -Wno-missing-field-initializers -DDEBUG=1 \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 \
    -I "$JUCE/modules" -I "$LIBGIT2/include" \
    -x objective-c++ "$JUCE/modules/juce_core/juce_core.mm" \
    -x c++ "$ROOT/Projucer/Source/Git/jucer_GitCommand.cpp" \
    "$ROOT/Projucer/Source/Git/Tests/git_selfcheck.cpp" \
    -x objective-c++ "$ROOT/Projucer/Source/AI/jucer_Keychain.mm" \
    -x none "$LIBGIT2/build-macos/libgit2.a" \
    -lz -liconv \
    -framework Foundation -framework Security -framework CoreFoundation \
    -framework CoreServices -framework IOKit -framework Cocoa \
    -o "$OUT/git_selfcheck"

"$OUT/git_selfcheck"
