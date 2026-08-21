# iPadOS native menu bar — findings

Investigated 2026-08-20, against JUCE 9.0.0. No code was changed.

## The question

iPadOS 26 gives every app a native menu bar. Projucer on iPad currently shows
two bars: JUCE's own `MenuBarComponent`, and the OS one (normally hidden behind
the top edge). Can Projucer use the native one instead?

## What is actually there today

JUCE has **no iOS native-menu support at all**:

- No occurrence of `buildMenuWithBuilder:`, `UIMenuBuilder`, `UIMenuSystem`,
  `UIDeferredMenuElement` or `UIKeyCommand` anywhere in `JUCE/modules`.
- `MenuBarModel::setMacMainMenu()` is inside `#if JUCE_MAC`; there is no iOS
  counterpart.
- The only native menu implementation is `juce_MainMenu_mac.mm`.

So the OS bar we see is UIKit's empty default skeleton (App / Edit / Window /
Help), handed to every iPadOS 26 app and completely disconnected from Projucer.
Projucer draws its own bar because `MainWindow` calls `setMenuBar()` under
`#if ! JUCE_MAC`. Nothing is wrong with either bar; they simply do not know
about each other.

## Hardware keyboard shortcuts already work

Worth recording because it is easy to assume otherwise: JUCE's iOS peer
implements `pressesBegan:` (`juce_UIViewComponentPeer_ios.mm:1055`) and routes
through `handleKeyPress` / `handleKeyUpOrDown`, with modifiers read from
`[event modifierFlags]`. Command-Z from a Magic Keyboard reaches
`KeyPressMappingSet` today. What is missing is only the *software* keyboard's
lack of modifier keys, which is why the on-screen assist bar exists.

Adopting the native menu bar therefore does **not** unlock shortcuts. It would
buy discoverability: Projucer's commands appearing in the OS bar and in the
Command-hold shortcut overlay, plus the ability to strip UIKit's irrelevant
default menus.

## Feasibility

### Hooking into UIKit needs no JUCE change

`buildMenuWithBuilder:` is a `UIResponder` method that UIKit calls on the app
delegate. Two ways in:

1. JUCE's documented custom-delegate hook — `juce_GetIOSCustomDelegateClass()`
   / `JUCEApplicationBase::iOSCustomDelegate` in `juce_Initialisation.h:121`.
   It requires forwarding *every* delegate message to JUCE's own delegate, and
   the header itself warns that subtle bugs follow. Too much surface for one
   method.
2. Add the method to `JuceAppStartupDelegate` at runtime with
   `class_addMethod`, then `[UIMenuSystem.mainSystem setNeedsRebuild]`. This is
   the same objc-runtime approach already used for `UIEvent.modifierFlags` in
   `jucer_TouchSupport.cpp`, needs only a plain `.cpp`, and leaves the JUCE
   tree untouched.

### Mapping MenuBarModel onto UIMenu

Everything has a counterpart:

| JUCE | UIKit |
|---|---|
| `getMenuBarNames()` | top-level `UIMenu`s via `insertChildMenu:atStartOfMenuForIdentifier:` |
| `PopupMenu` item | `UIAction` whose handler calls `menuItemSelected` |
| separator | nested `UIMenu` with `.displayInline` |
| sub-menu | nested `UIMenu` |
| ticked / disabled | `UIMenuElement.state` / `.disabled` |
| `ApplicationCommandInfo::defaultKeypresses` | `UIKeyCommand` |

The one real mismatch is lifetime. JUCE builds each menu lazily, when it is
opened, in `getMenuForIndex()`. UIKit wants the whole tree up front. Projucer
leans on the lazy model heavily: recent files, the open-projects list, colour
schemes. `UIDeferredMenuElement.uncached(elementProvider:)` (iOS 15+) is
re-evaluated every time the menu is shown and maps almost exactly onto
`getMenuForIndex()`, so this is tractable. Without it the fallback is calling
`setNeedsRebuild` on every `menuItemsChanged()`, which is coarser but works.

## Reasons not to rush

- **The native bar is hidden by default.** It appears on a swipe or pointer at
  the top edge. For a menu-driven app driven by touch, that is worse
  discoverability than the always-visible bar. This could make the app harder
  to use, not easier.
- **iPadOS 26 only.** Earlier versions show the bar only with a hardware
  keyboard attached, so both paths would be needed anyway. The current
  deployment target is 12.0.
- **The bridge belongs in JUCE, not here.** A full `MenuBarModel` to `UIMenu`
  bridge is module-level code. Carrying it in a forked Projucer is a
  maintenance burden; it should be an upstream contribution.
- Whether the OS bar can be hidden outright was not established. Default menu
  *items* can be removed via the builder; suppressing the bar itself is
  unconfirmed.

## If some of this is wanted cheaply

Implementing `buildMenuWithBuilder:` purely to remove UIKit's irrelevant
default menus, and to publish Projucer's commands as `UIKeyCommand`s, is a
small self-contained change that leaves the existing menu bar alone. That is
the only part with a clear cost/benefit case today.
