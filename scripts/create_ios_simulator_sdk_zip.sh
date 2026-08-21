#!/bin/sh

set -eu

usage()
{
    echo "Usage: $0 [SDK_PATH] [OUTPUT_ZIP]" >&2
    echo "  SDK_PATH   defaults to xcrun --sdk iphonesimulator --show-sdk-path" >&2
    echo "  OUTPUT_ZIP defaults to OnDeviceBuild/build-artifacts/iPhoneSimulator.sdk.zip" >&2
    exit 2
}

if [ "$#" -gt 2 ]; then
    usage
fi

sdk_path=${1:-$(xcrun --sdk iphonesimulator --show-sdk-path)}
output_zip=${2:-$(pwd)/OnDeviceBuild/build-artifacts/iPhoneSimulator.sdk.zip}

if [ ! -d "$sdk_path" ]; then
    echo "SDK directory not found: $sdk_path" >&2
    exit 1
fi

# xcrun normally returns the versioned SDK symlink. Resolve it before archiving
# so the zip contains the SDK contents, not the symlink itself.
sdk_path=$(cd "$sdk_path" && pwd -P)

if [ ! -f "$sdk_path/usr/lib/libSystem.tbd" ]; then
    echo "SDK is missing usr/lib/libSystem.tbd: $sdk_path" >&2
    exit 1
fi

if [ ! -d "$sdk_path/System/Library/Frameworks/UIKit.framework" ]; then
    echo "SDK is missing UIKit.framework: $sdk_path" >&2
    exit 1
fi

output_dir=$(dirname "$output_zip")
mkdir -p "$output_dir"

temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/projucer-ios-sdk.XXXXXX")
trap 'rm -rf "$temp_dir"' EXIT HUP INT TERM

temp_zip="$temp_dir/iPhoneSimulator.sdk.zip"

echo "SDK: $sdk_path"
echo "Output: $output_zip"
echo "Creating archive..."

# Archive the contents of the SDK directory so ZipStore extracts sentinel files
# directly under Documents/sdk-simulator.  Do not preserve symlinks here:
# ZipStore is intentionally a small, portable extractor and materializes zip
# entries as regular files/directories.  The SDK contains headers such as
# usr/include/pthread.h that are symlinks, so preserving them would extract the
# link target text instead of the header contents.
#
# zip follows symlinks by default; -y would preserve them and must not be used.
(cd "$sdk_path" && /usr/bin/zip -q -X -r "$temp_zip" *)

/usr/bin/unzip -tq "$temp_zip"
if ! /usr/bin/unzip -Z1 "$temp_zip" | /usr/bin/grep -Fqx 'usr/lib/libSystem.tbd'; then
    echo "Archive is missing usr/lib/libSystem.tbd" >&2
    exit 1
fi

if ! /usr/bin/unzip -Z1 "$temp_zip" | /usr/bin/grep -Fqx 'System/Library/Frameworks/UIKit.framework/'; then
    echo "Archive is missing UIKit.framework" >&2
    exit 1
fi

mv -f "$temp_zip" "$output_zip"
echo "Created $(du -h "$output_zip" | awk '{print $1}') archive."
