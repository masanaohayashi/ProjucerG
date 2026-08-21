#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
args=(--platform device)
if [ "$#" -eq 1 ] && { [ "$1" = "-h" ] || [ "$1" = "--help" ]; }; then
    exec "$SCRIPT_DIR/create_ios_sdk_zip.sh" "${args[@]}" --help
fi
if [ "$#" -gt 2 ]; then
    echo "Usage: $0 [SDK_PATH] [OUTPUT_ZIP]" >&2
    exit 2
fi
if [ "$#" -ge 1 ]; then
    args+=(--sdk-path "$1")
fi
if [ "$#" -ge 2 ]; then
    args+=(--output "$2")
fi
exec "$SCRIPT_DIR/create_ios_sdk_zip.sh" "${args[@]}"
