#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_FILE="$ROOT_DIR/Projucer/Builds/MacOSX/Projucer.xcodeproj"
SCHEME="${PROJUCERG_XCODE_SCHEME:-Projucer - App}"
CONFIGURATION="${PROJUCERG_CONFIGURATION:-Release}"
DERIVED_DATA_PATH="${PROJUCERG_DERIVED_DATA_PATH:-$ROOT_DIR/Projucer/Builds/MacOSX/DerivedData}"
BUILD_DIR="$ROOT_DIR/Projucer/Builds/MacOSX/build"
APP_PATH="${PROJUCERG_APP_PATH:-$BUILD_DIR/$CONFIGURATION/Projucer.app}"
DIST_DIR="${PROJUCERG_DIST_DIR:-$ROOT_DIR/dist/macos}"
ZIP_PATH="${PROJUCERG_ZIP_PATH:-$DIST_DIR/ProjucerG-macOS-$CONFIGURATION.zip}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "error: macOS is required." >&2
  exit 1
fi

mkdir -p "$DIST_DIR"

xcodebuild \
  -project "$PROJECT_FILE" \
  -scheme "$SCHEME" \
  -configuration "$CONFIGURATION" \
  -derivedDataPath "$DERIVED_DATA_PATH" \
  build

if [[ ! -d "$APP_PATH" ]]; then
  echo "error: app bundle not found: $APP_PATH" >&2
  exit 1
fi

if [[ -n "${PROJUCERG_DEVELOPER_ID_APPLICATION:-}" ]]; then
  codesign --force --deep --options runtime --timestamp \
    --sign "$PROJUCERG_DEVELOPER_ID_APPLICATION" \
    "$APP_PATH"

  codesign --verify --deep --strict --verbose=2 "$APP_PATH"
  spctl --assess --type execute --verbose "$APP_PATH"
else
  echo "warning: PROJUCERG_DEVELOPER_ID_APPLICATION is not set; skipping Developer ID signing." >&2
  echo "warning: notarization will fail unless the app is signed with Developer ID." >&2
fi

rm -f "$ZIP_PATH"
ditto -c -k --keepParent "$APP_PATH" "$ZIP_PATH"

echo "App: $APP_PATH"
echo "Zip: $ZIP_PATH"
