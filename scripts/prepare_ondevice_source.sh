#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/ios_setup_common.sh
source "$SCRIPT_DIR/ios_setup_common.sh"

source_root="$IOS_SETUP_PROJECT_ROOT/OnDeviceBuildSource"
generated_root="$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild"

usage()
{
    cat <<'USAGE'
Usage: prepare_ondevice_source.sh

Create the generated OnDeviceBuild source tree from the tracked
OnDeviceBuildSource template. OnDeviceBuild may be absent before this command.
USAGE
}

if [ "$#" -gt 0 ]; then
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        *)
            ios_setup_die "Unknown option: $1"
            ;;
    esac
fi

for required_file in \
    CMakeLists.txt \
    .gitignore \
    host/main.cpp \
    include/OnDeviceBuild/Engine.h \
    src/Engine.cpp \
    tests/CMakeLists.txt \
    tests/fixtures/mini-manifest.json; do
    ios_setup_require_file "$source_root/$required_file"
done

mkdir -p "$generated_root"
cp -p "$source_root/CMakeLists.txt" "$generated_root/CMakeLists.txt"
cp -p "$source_root/.gitignore" "$generated_root/.gitignore"

for source_dir in host include src tests; do
    mkdir -p "$generated_root/$source_dir"
    cp -R "$source_root/$source_dir/." "$generated_root/$source_dir/"
done

ios_setup_log "OnDeviceBuild source prepared from OnDeviceBuildSource"
