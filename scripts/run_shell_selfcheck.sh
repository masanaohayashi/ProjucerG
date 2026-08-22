#!/bin/bash
# プロセス内シェルの純粋ロジックを juce_core + libgit2 でビルドして実行する。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JUCE="$ROOT/OnDeviceBuild/dependencies/JUCE"
LIBGIT2="$ROOT/OnDeviceBuild/third_party/libgit2"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

if [ ! -d "$JUCE/modules/juce_core" ]; then
    echo "JUCE is missing at $JUCE. Run scripts/prepare_dependencies.sh first." >&2
    exit 1
fi

if [ ! -f "$LIBGIT2/build-macos/libgit2.a" ]; then
    echo "libgit2 for macOS is missing. Run scripts/build_libgit2.sh first." >&2
    exit 1
fi

clang++ -std=c++17 -g -Wall -Wextra -Wno-missing-field-initializers -DDEBUG=1 \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 \
    -I "$JUCE/modules" \
    -I "$LIBGIT2/include" \
    -I "$ROOT/Projucer/Source" \
    -x objective-c++ "$JUCE/modules/juce_core/juce_core.mm" \
    -x c++ \
    "$ROOT/Projucer/Source/Shell/jucer_InProcessShell.cpp" \
    "$ROOT/Projucer/Source/Shell/jucer_ShellApplets.cpp" \
    "$ROOT/Projucer/Source/Git/jucer_GitCommand.cpp" \
    "$ROOT/Projucer/Source/Shell/Tests/shell_selfcheck.cpp" \
    -x objective-c++ "$ROOT/Projucer/Source/AI/jucer_Keychain.mm" \
    -x none "$LIBGIT2/build-macos/libgit2.a" \
    -lz -liconv \
    -framework Foundation -framework Security -framework CoreFoundation \
    -framework CoreServices -framework IOKit -framework Cocoa \
    -o "$OUT/shell_selfcheck"

"$OUT/shell_selfcheck"
