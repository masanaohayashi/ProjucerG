#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/ios_setup_common.sh
source "$SCRIPT_DIR/ios_setup_common.sh"

ZSIGN_REPOSITORY=${ZSIGN_REPOSITORY:-https://github.com/zhlynn/zsign.git}
ZSIGN_COMMIT=${ZSIGN_COMMIT:-e803f870dc686e6161d00d9b22c425b8acdfacee}
zsign_destination="$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/third_party/zsign"
force=0

usage()
{
    cat <<'USAGE'
Usage: fetch_third_party.sh [options]

Options:
  --force                 Re-fetch zsign at the pinned commit
  --destination PATH     Change the zsign destination
  -h, --help              Show this help

Override the repository with ZSIGN_REPOSITORY and the pinned commit with ZSIGN_COMMIT.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --force)
            force=1
            shift
            ;;
        --destination)
            [ "$#" -ge 2 ] || ios_setup_die "--destination requires a value"
            zsign_destination=$2
            shift 2
            ;;
        --destination=*)
            zsign_destination=${1#*=}
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            ios_setup_die "Unknown option: $1"
            ;;
    esac
done

ios_setup_require_command git

required_files=(
    LICENSE
    src/archo.cpp
    src/common/json.cpp
    src/common/base64.cpp
    src/third-party/zlib/zlib.h
    src/third-party/minizip/zip.c
)

has_required_files()
{
    local required_file
    for required_file in "${required_files[@]}"; do
        [ -f "$zsign_destination/$required_file" ] || return 1
    done
}

has_expected_revision()
{
    local revision_file="$zsign_destination/.projucer-source-revision"
    has_required_files || return 1
    [ -f "$revision_file" ] || return 1
    [ "$(tr -d '\r\n' < "$revision_file")" = "$ZSIGN_COMMIT" ]
}

if [ "$force" -eq 0 ] && has_expected_revision; then
    ios_setup_log "third_party/zsign: using existing source"
    ios_setup_log "Use --force to update it"
    exit 0
fi

started_at=$(date +%s)
temporary_root=$(mktemp -d)
checkout_root="$temporary_root/zsign"
staging_root="$temporary_root/staged-zsign"

cleanup()
{
    rm -rf "$temporary_root"
}
trap cleanup EXIT

ios_setup_log "third_party/zsign: fetch started"
ios_setup_log "  repository: $ZSIGN_REPOSITORY"
ios_setup_log "  commit:     $ZSIGN_COMMIT"

git init -q "$checkout_root"
git -C "$checkout_root" remote add origin "$ZSIGN_REPOSITORY"
git -C "$checkout_root" fetch --quiet --depth 1 origin "$ZSIGN_COMMIT"
git -C "$checkout_root" checkout --quiet --detach FETCH_HEAD

checked_out_commit=$(git -C "$checkout_root" rev-parse HEAD)
for required_file in "${required_files[@]}"; do
    [ -f "$checkout_root/$required_file" ] || ios_setup_die "zsign is missing required file: $required_file"
done

mkdir -p "$staging_root"
mkdir -p "$staging_root/zsign"
cp -R "$checkout_root/src" "$staging_root/zsign/src"
cp "$checkout_root/LICENSE" "$staging_root/zsign/LICENSE"
printf '%s\n' "$checked_out_commit" > "$staging_root/zsign/.projucer-source-revision"

mkdir -p "$(dirname "$zsign_destination")"
if [ -e "$zsign_destination" ]; then
    rm -rf "$zsign_destination"
fi
mv "$staging_root/zsign" "$zsign_destination"

ios_setup_log "third_party/zsign: installed $checked_out_commit in $(ios_setup_format_duration $(( $(date +%s) - started_at )))"
