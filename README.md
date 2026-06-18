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

## Custom LookAndFeel

In addition to the built-in `LookAndFeel_V1`-`V4` choices, ProjucerG can offer
header-only LookAndFeel classes that are bundled with Projucer itself, selectable
as a "Custom LookAndFeel". Whichever one is selected gets copied into
`JuceLibraryCode/` automatically every time the project is saved/exported, so the
generated project builds standalone with no extra setup.

### Using a Custom LookAndFeel in a project

1. Open the project in Projucer and pick a class (e.g. `IfwTabbedLookAndFeel`) from
   the **"Custom LookAndFeel"** dropdown in Project Settings. Leaving it as
   `<None>` changes nothing.
2. Once selected, that class is added to the choices for the project-wide
   **"Default LookAndFeel"** setting and for each component's **"LookAndFeel"**
   property in the GUI Editor. Use it like any other LookAndFeel choice from there.
3. On save, `JuceLibraryCode/<ClassName>.h` is generated automatically and the
   generated code `#include`s it. No manual file copying or search path setup is
   required.

### Adding a new Custom LookAndFeel

Custom LookAndFeels are driven by a small registry baked into Projucer itself as
BinaryData. Adding a new one requires editing this repository's Projucer source:

1. Put the new header-only LookAndFeel class (e.g. `MyLookAndFeel.h`) in
   `Projucer/Source/BinaryData/Templates/`.
2. Add a FILE entry for it to the `BinaryData > Templates` group in
   `Projucer/Projucer.jucer`, matching the existing `IfwTabbedLookAndFeel.h` entry
   (`compile="0" resource="1"`).
3. Add an entry to the registry array in
   `Projucer/Source/Project/jucer_CustomLookAndFeels.h`:

   ```cpp
   CustomLookAndFeelInfo { "MyLookAndFeel", "MyLookAndFeel.h", "MyLookAndFeel_h" }
   ```

   `binaryDataResourceName` follows Projucer's BinaryData naming convention: the
   filename with its `.` replaced by `_`.
4. In both `Projucer/Source/ComponentEditor/jucer_JucerDocument.cpp` and
   `Projucer/Source/ComponentEditor/Components/jucer_ComponentTypeHandler.cpp`,
   add an `#include` for the new header next to the existing
   `IfwTabbedLookAndFeel.h` include, and add one line to each file's
   `createCustomLookAndFeel()`:

   ```cpp
   if (type == "MyLookAndFeel") return std::make_unique<MyLookAndFeel>();
   ```

   (This is the spot that constructs the real C++ type for the GUI Editor's live
   preview, so it can't be fully data-driven from the registry alone — one line per
   class is still needed.)
5. Rebuild Projucer, then use that freshly built Projucer to resave
   `Projucer.jucer` so `JuceLibraryCode/BinaryData.cpp/.h` picks up the new
   resource:

   ```sh
   xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
     -scheme "Projucer - App" -configuration Debug -quiet build

   Projucer/Builds/MacOSX/build/Debug/Projucer.app/Contents/MacOS/Projucer \
     --resave Projucer/Projucer.jucer
   ```

From then on, `MyLookAndFeel` shows up in the "Custom LookAndFeel" dropdown for any
project, and selecting and saving it copies the header automatically as described
above.

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
