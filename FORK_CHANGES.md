# Fork Changes

This fork extends Projucer's GUI editor with practical support for several JUCE
components and workflows that are limited or missing in the original Projucer.

## GUI Editor Improvements

- Added preview and generated-code support for `ImageButton` images.
- Added support for image paint elements in the Graphics editor, including
  stretch modes.
- Added `ImageComponent` and `DrawableButton` support.
- Added SVG resource selection for `DrawableButton`.
- Added `TextEditor` placeholder text support.
- Added rotary filmstrip slider support with image, frame count, and orientation
  settings.
- Added optional AffineTransform-based scaling for GUI components, with
  scale-to-fit and keep-aspect-ratio modes.
- Added embedded custom font support: `.ttf`/`.otf` files added to the project as
  binary resources are listed in the font selectors of Label and Text paint
  elements, previewed in the editor without being installed on the system, and
  loaded from `BinaryData` in the generated code.
- Added a `Separator` component: a draggable splitter bar that resizes the two
  components either side of it. The neighbours, minimum sizes, bar colour and
  the behaviour on parent resize (both halves proportional, or one half kept at
  a fixed size) are all set in the GUI editor, and the generated code carries a
  self-contained `JucerSeparatorBar` class so no hand-written code is needed.

## Template Improvements

- Updated generated GUI Application templates so `MainComponent` can be edited
  in the GUI editor.
- Updated Plugin Basic and Plugin ARA editor templates so
  `AudioProcessorEditor` can be edited in the GUI editor.
- Updated Application - Audio templates for GUI editor compatibility.

## LookAndFeel Support

- Added project-level, GUI document-level, and per-component built-in
  LookAndFeel selection.
- Added generated-code support for selected LookAndFeel instances.
- Added bundled custom LookAndFeel support, including:
  - a bundled `IfwTabbedLookAndFeel` test LookAndFeel,
  - GUI editor preview support,
  - automatic header generation into `JuceLibraryCode`,
  - automatic generated-code includes,
  - project save/export integration.

## Apple Exporter Support

- Added Xcode exporter support for App Groups, iCloud Documents, shared
  preference read/write domains, `LSApplicationCategoryType`, and related
  `Info.plist` / entitlements generation.

## Exporter Support

- Added per-file exporter exclusions, allowing individual project files to be
  omitted from selected exporters while remaining available to others.

## Examples

- Added and updated example projects used to verify GUI editor behaviour,
  LookAndFeel selection, scaling, and generated-code output.

## Built-in git

- `Projucer/Source/Git/jucer_GitCommand.cpp` translates a git command line into
  libgit2 calls and runs it inside the editor process, so version control works on
  iOS where no shell exists.
- The AI agent gets a `git` tool wired straight to it; on iOS `exec_command` also
  routes git through the same code.
- HTTPS with a Keychain-stored token only; merges are fast-forward only.
- Build the static libraries with `scripts/build_libgit2.sh`, check them with
  `scripts/run_git_selfcheck.sh`.
