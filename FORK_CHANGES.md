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

## Examples

- Added and updated example projects used to verify GUI editor behaviour,
  LookAndFeel selection, scaling, and generated-code output.
