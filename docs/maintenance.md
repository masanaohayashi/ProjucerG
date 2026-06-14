# Maintenance Workflow

This document describes how to update ProjucerG when a new upstream JUCE
version is released.

ProjucerG should treat the upstream JUCE Projucer as the base, then reapply the
GUI Editor restoration changes on top. This keeps the fork close to upstream and
avoids carrying obsolete Projucer code forward by accident.

## Update Strategy

For each new JUCE release:

1. Start from the new upstream `extras/Projucer` source tree.
2. Reapply the ProjucerG GUI Editor restoration changes.
3. Update `Projucer/Projucer.jucer` to point at the new local JUCE checkout.
4. Regenerate project files with Projucer `--resave`.
5. Build and smoke-test the macOS Debug app.
6. Commit, tag, and push the result.

This is closer to a patch-based workflow than a long-running merge workflow.
Most upstream Projucer changes should be accepted as-is, and only the
ProjucerG-specific GUI Editor integration should be carried across releases.

## Local Checkout Naming

Keep upstream JUCE checkouts next to this repository and do not track them in
git.

Recommended names:

```text
../JUCE-8.0.13
../JUCE-9.0.0
../JUCE-juce9
```

Use release-numbered directories for official releases. Use branch names only
for preview or development tracking.

## Branches And Tags

Recommended convention:

- `master`: main development branch.
- `juce9`: JUCE 9 preview tracking branch until JUCE 9 is officially released.
- `8.0.13`, `9.0.0`, etc.: tags for release-based ProjucerG snapshots.
- Optional maintenance branches can be created if a release line needs fixes
  after the main branch has moved on.

When JUCE 9 is officially released, create a fresh branch from the current
development branch, update to the official JUCE 9 release checkout, verify it,
then tag the resulting commit with the upstream JUCE version.

## Update Procedure

1. Clone or update the upstream JUCE checkout.

   ```sh
   git clone --depth 1 --branch 9.0.0 https://github.com/juce-framework/JUCE.git ../JUCE-9.0.0
   ```

2. Create a working branch.

   ```sh
   git switch -c juce-9.0.0-update
   ```

3. Replace `Projucer/` with the upstream Projucer source tree.

   Copy from:

   ```text
   ../JUCE-9.0.0/extras/Projucer/
   ```

   into:

   ```text
   Projucer/
   ```

   Preserve or restore `Projucer/Source/ComponentEditor/`, because upstream
   JUCE 8 and later do not ship the legacy GUI Editor integration as an active
   Projucer feature.

4. Reapply the ProjucerG-specific changes.

   The main integration points are:

   - `Projucer/Source/ComponentEditor/`
   - `Projucer/Source/Application/jucer_Application.cpp`
   - `Projucer/Source/Application/jucer_Application.h`
   - `Projucer/Source/Application/jucer_CommandIDs.h`
   - `Projucer/Source/CodeEditor/jucer_OpenDocumentManager.cpp`
   - `Projucer/Source/Project/UI/jucer_ProjectContentComponent.cpp`
   - `Projucer/Source/Project/UI/jucer_ProjectContentComponent.h`
   - `Projucer/Source/Utility/Helpers/jucer_PresetIDs.h`
   - `Projucer/Projucer.jucer`

   The expected restored features are:

   - GUI Editor document type registration.
   - `GUI Editor` menu.
   - `GUI Editor Enabled` command and stored preference.
   - `New GUI Component` command.
   - Legacy Component Editor source files.
   - Right-edge and bottom-right resize bug fix.

5. Update the JUCE paths in `Projucer/Projucer.jucer`.

   Example:

   ```text
   ../JUCE-9.0.0/modules
   ../JUCE-9.0.0/extras/Build
   ```

6. Regenerate project files.

   ```sh
   Projucer/Builds/MacOSX/build/Debug/Projucer.app/Contents/MacOS/Projucer --resave Projucer/Projucer.jucer
   ```

   If the existing built Projucer cannot resave the project after an upstream
   change, build the upstream Projucer first and use that binary for the initial
   resave.

7. Build the macOS Debug app.

   ```sh
   xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
     -scheme "Projucer - App" \
     -configuration Debug \
     -derivedDataPath Projucer/Builds/MacOSX/DerivedData \
     -quiet build
   ```

8. Resave once more using the newly built ProjucerG binary.

   ```sh
   Projucer/Builds/MacOSX/build/Debug/Projucer.app/Contents/MacOS/Projucer --resave Projucer/Projucer.jucer
   ```

9. Run a smoke test.

   Check at least:

   - Projucer launches.
   - A `.jucer` project opens.
   - GUI Editor can be enabled and disabled.
   - GUI files open in the GUI Editor when enabled.
   - GUI files open in the code editor when disabled.
   - `New GUI Component` creates the expected files.
   - Right-edge and bottom-right Component Editor resizing works.
   - Normal source files still open in the code editor.

10. Commit and tag.

    ```sh
    git add -A
    git commit -m "Update ProjucerG to JUCE 9.0.0"
    git tag -a 9.0.0 -m "ProjucerG based on JUCE 9.0.0"
    ```

11. Push the branch and tag.

    ```sh
    git push origin HEAD
    git push origin 9.0.0
    ```

## macOS Release And Notarization

Release build and notarization are split into two scripts:

```text
scripts/build_release_macos.sh
scripts/notarize_macos.sh
```

The build script builds the Release app, optionally signs it with a Developer ID
Application certificate, verifies the signature, and creates a zip suitable for
notarization.

Set the signing identity before running it:

```sh
export PROJUCERG_DEVELOPER_ID_APPLICATION="Developer ID Application: Your Name (TEAMID)"
scripts/build_release_macos.sh
```

If `PROJUCERG_DEVELOPER_ID_APPLICATION` is omitted, the script still builds and
zips the app, but notarization will fail until the app is signed.

Create a notarytool keychain profile once:

```sh
xcrun notarytool store-credentials "ProjucerG-notary" \
  --apple-id "you@example.com" \
  --team-id "TEAMID" \
  --password "app-specific-password"
```

Then submit the zip, staple the app, and verify Gatekeeper assessment:

```sh
export PROJUCERG_NOTARY_PROFILE="ProjucerG-notary"
scripts/notarize_macos.sh
```

Useful overrides:

- `PROJUCERG_CONFIGURATION`: defaults to `Release`.
- `PROJUCERG_XCODE_SCHEME`: defaults to `Projucer - App`.
- `PROJUCERG_DERIVED_DATA_PATH`: defaults to
  `Projucer/Builds/MacOSX/DerivedData`.
- `PROJUCERG_APP_PATH`: defaults to the built `Projucer.app`.
- `PROJUCERG_DIST_DIR`: defaults to `dist/macos`.
- `PROJUCERG_ZIP_PATH`: defaults to
  `dist/macos/ProjucerG-macOS-Release.zip`.

## Patch Notes

The GUI Editor restoration should remain a small, reviewable patch on top of
upstream Projucer. If future updates become difficult, create explicit patch
files under `patches/` that separate:

- Restoring `Source/ComponentEditor/`.
- Reconnecting GUI Editor commands, menus, and preferences.
- Adding `New GUI Component`.
- Fixing legacy Component Editor resizing.
- Updating `Projucer.jucer`.

Keeping these concerns separate will make future upstream release updates easier
to review and reapply.
