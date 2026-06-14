# ProjucerG

ProjucerG is an unofficial fork of the JUCE Projucer focused on restoring the
legacy GUI Editor that was available in JUCE 7 and deprecated/removed from the
JUCE 8 Projucer.

This project is not an official JUCE project and is not endorsed by Raw Material
Software Limited.

## Current Status

The `master` branch currently contains the JUCE 8 based version.

Implemented changes include:

- Restored the legacy GUI Editor source files from JUCE 7.
- Reconnected the GUI Editor document type and menus in the JUCE 8 Projucer.
- Added the GUI Editor enable/disable preference.
- Added "New GUI Component" support.
- Updated GUI Editor font code to use JUCE 8 `FontOptions`.
- Fixed a legacy GUI Editor resizing bug where right-edge and bottom-right
  resizing could snap back immediately.
- Verified macOS Debug builds and Projucer `--resave` stability.

## Branches

Planned branch structure:

- `master`: current JUCE 8 based branch.
- `juce8`: JUCE 8 based maintenance branch, if split from `master`.
- `juce9`: future JUCE 9 based branch.

## Repository Layout

- `Projucer/`: the modified Projucer source tree.
- `design.md`: Japanese design and implementation notes.
- `JUCE7_JUCE8_modules_diff.md`: Japanese notes on JUCE module differences.
- `JUCE7_JUCE8_Projucer_diff.md`: Japanese notes on Projucer differences.

The local JUCE checkout directories used during development are intentionally
not tracked by git.

## Build Requirements

This branch expects a JUCE 8 checkout next to this repository. The current
development setup used:

```text
../JUCE-8.0.13
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

## License

ProjucerG contains code derived from the JUCE Framework and the JUCE Projucer.
JUCE is copyright Raw Material Software Limited and its contributors.

Unless you have a separate commercial JUCE licence from Raw Material Software
Limited that permits your intended use, the JUCE-derived code in this repository
is distributed under the GNU Affero General Public License version 3.

See [LICENSE.md](LICENSE.md) for details.
