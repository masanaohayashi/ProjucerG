#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/ios_setup_common.sh
source "$SCRIPT_DIR/ios_setup_common.sh"

platform=simulator
sdk_path=
output_zip=

usage()
{
    cat <<'USAGE'
Usage: create_ios_sdk_zip.sh [options]

Options:
  --platform device|simulator  SDK to archive (default: simulator)
  --sdk-path PATH              Explicit Xcode SDK directory
  --output PATH                Output zip path
  -h, --help                   Show this help
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --platform)
            [ "$#" -ge 2 ] || ios_setup_die "--platform requires a value"
            platform=$2
            shift 2
            ;;
        --platform=*)
            platform=${1#*=}
            shift
            ;;
        --sdk-path)
            [ "$#" -ge 2 ] || ios_setup_die "--sdk-path requires a value"
            sdk_path=$2
            shift 2
            ;;
        --sdk-path=*)
            sdk_path=${1#*=}
            shift
            ;;
        --output)
            [ "$#" -ge 2 ] || ios_setup_die "--output requires a value"
            output_zip=$2
            shift 2
            ;;
        --output=*)
            output_zip=${1#*=}
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

case "$platform" in
    device)
        sdk_name=iphoneos
        archive_name=iPhoneOS.sdk.zip
        ;;
    simulator)
        sdk_name=iphonesimulator
        archive_name=iPhoneSimulator.sdk.zip
        ;;
    *)
        ios_setup_die "--platform must be device or simulator"
        ;;
esac

if [ -z "$sdk_path" ]; then
    sdk_path=$(xcrun --sdk "$sdk_name" --show-sdk-path)
fi

if [ -z "$output_zip" ]; then
    output_zip="$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-artifacts/$archive_name"
elif [[ "$output_zip" != /* ]]; then
    output_zip="$IOS_SETUP_PROJECT_ROOT/$output_zip"
fi

ios_setup_require_dir "$sdk_path"
sdk_path=$(cd "$sdk_path" && pwd -P)
ios_setup_require_file "$sdk_path/usr/lib/libSystem.tbd"
ios_setup_require_dir "$sdk_path/System/Library/Frameworks/UIKit.framework"

output_dir=$(dirname "$output_zip")
mkdir -p "$output_dir"

temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/projucer-ios-sdk.XXXXXX")
temp_zip="$temp_dir/$archive_name"
cleanup()
{
    rm -rf "$temp_dir"
}
trap cleanup EXIT HUP INT TERM

file_count=$(find "$sdk_path" -type f -print | wc -l | tr -d ' ')
started_at=$(date +%s)
ios_setup_log "$platform SDK: $sdk_path"
ios_setup_log "Output: $output_zip"
ios_setup_log "Files to archive: $file_count"

(cd "$sdk_path" && /usr/bin/zip -q -X -r "$temp_zip" *)
# Some Xcode SDKs expose libSystem.tbd only as a relative symlink.  When the
# SDK root is passed as a wildcard, macOS zip can omit that symlink while still
# archiving its containing directory.  Add the exact entry explicitly because
# ZipStore and the setup check use this stable SDK sentinel.
(cd "$sdk_path" && /usr/bin/zip -q -X "$temp_zip" usr/lib/libSystem.tbd)
/usr/bin/unzip -tq "$temp_zip"

if ! /usr/bin/unzip -Z1 "$temp_zip" | /usr/bin/grep -Fx 'usr/lib/libSystem.tbd' >/dev/null; then
    ios_setup_die "Archive is missing usr/lib/libSystem.tbd"
fi
if ! /usr/bin/unzip -Z1 "$temp_zip" | /usr/bin/grep -Fx 'System/Library/Frameworks/UIKit.framework/' >/dev/null; then
    ios_setup_die "Archive is missing UIKit.framework"
fi

mv -f "$temp_zip" "$output_zip"
ios_setup_log "SDK zip completed: $(du -h "$output_zip" | awk '{print $1}'), elapsed $(ios_setup_format_duration $(( $(date +%s) - started_at )))"
