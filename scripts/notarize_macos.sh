#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIGURATION="${PROJUCERG_CONFIGURATION:-Release}"
APP_PATH="${PROJUCERG_APP_PATH:-$ROOT_DIR/Projucer/Builds/MacOSX/build/$CONFIGURATION/Projucer.app}"
ZIP_PATH="${PROJUCERG_ZIP_PATH:-$ROOT_DIR/dist/macos/ProjucerG-macOS-$CONFIGURATION.zip}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "error: macOS is required." >&2
  exit 1
fi

if [[ -z "${PROJUCERG_NOTARY_PROFILE:-}" ]]; then
  echo "error: PROJUCERG_NOTARY_PROFILE is not set." >&2
  echo "Create one with: xcrun notarytool store-credentials <profile-name>" >&2
  exit 1
fi

if [[ ! -f "$ZIP_PATH" ]]; then
  echo "error: notarization zip not found: $ZIP_PATH" >&2
  echo "Run scripts/build_release_macos.sh first." >&2
  exit 1
fi

if [[ ! -d "$APP_PATH" ]]; then
  echo "error: app bundle not found: $APP_PATH" >&2
  exit 1
fi

xcrun notarytool submit "$ZIP_PATH" \
  --keychain-profile "$PROJUCERG_NOTARY_PROFILE" \
  --wait

xcrun stapler staple "$APP_PATH"
xcrun stapler validate "$APP_PATH"
spctl --assess --type execute --verbose "$APP_PATH"

echo "Notarized app: $APP_PATH"

