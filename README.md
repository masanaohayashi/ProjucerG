# ProjucerG

ProjucerG is an unofficial fork of the JUCE Projucer focused on restoring the
legacy GUI Editor that was available in JUCE 7 and deprecated/removed from the
JUCE 8 Projucer.

This project is not an official JUCE project and is not endorsed by Raw Material
Software Limited.

## Current Status

The `juce9` branch contains the JUCE 9 preview based version.

Implemented changes include:

- Restored the legacy GUI Editor source files from JUCE 7.
- Reconnected the GUI Editor document type and menus in the JUCE 9 preview
  Projucer.
- Added the GUI Editor enable/disable preference.
- Added "New GUI Component" support.
- Updated GUI Editor font code to use `FontOptions`.
- Fixed a legacy GUI Editor resizing bug where right-edge and bottom-right
  resizing could snap back immediately.
- Verified macOS Debug builds and Projucer `--resave` stability.

## Branches

Planned branch structure:

- `master`: development branch.
- `8.0.13` tag: JUCE 8.0.13 based version.
- `juce9`: JUCE 9 preview based branch.

## Repository Layout

- `Projucer/`: the modified Projucer source tree.
- `docs/design.md`: Japanese design and implementation notes.
- `docs/maintenance.md`: maintainer workflow for tracking new upstream JUCE
  releases.
- `docs/JUCE7_JUCE8_modules_diff.md`: Japanese notes on JUCE module
  differences.
- `docs/JUCE7_JUCE8_Projucer_diff.md`: Japanese notes on Projucer
  differences.
- `docs/JUCE8_JUCE9_Projucer_diff.md`: Japanese notes on Projucer
  differences.

The local JUCE checkout directories used during development are intentionally
not tracked by git.

## Build Requirements

This branch expects the JUCE 9 preview checkout next to this repository. The
current development setup used:

```text
../JUCE-juce9
```

The Projucer project file is:

```text
Projucer/Projucer.jucer
```

The `.jucer` file is the source of truth for generated project files. After
editing it, regenerate exporters with:

```sh
Projucer/Builds/MacOSX/build/Debug/Projucer.app/Contents/MacOS/Projucer --resave Projucer/Projucer.jucer
```

On macOS, the Debug app can be built with:

```sh
xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
  -scheme "Projucer - App" \
  -configuration Debug \
  -derivedDataPath Projucer/Builds/MacOSX/DerivedData \
  -quiet build
```

For macOS Release builds and notarization, see:

```text
scripts/build_release_macos.sh
scripts/notarize_macos.sh
```

## License

ProjucerG contains code derived from the JUCE Framework and the JUCE Projucer.
JUCE is copyright Raw Material Software Limited and its contributors.

The JUCE 9 preview source files state that they may be used under the GNU Affero
General Public License version 3 and cannot be licensed commercially for the
preview.

See [LICENSE.md](LICENSE.md) for details.
