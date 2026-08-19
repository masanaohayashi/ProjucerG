/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/


#include "../../Application/jucer_Headers.h"
#include "../../Application/jucer_Application.h"
#include "jucer_TouchSupport.h"

#if JUCE_IOS
 #include <objc/runtime.h>
#endif

//==============================================================================
namespace
{
    /*  Feeds a key press into the focused component and then up its parent chain,
        the same route a real key event would take.
    */
    void sendKeyPressToFocusedComponent (const KeyPress& key)
    {
        for (auto* c = Component::getCurrentlyFocusedComponent(); c != nullptr; c = c->getParentComponent())
            if (c->keyPressed (key))
                return;
    }

   #if JUCE_IOS
    // UIKeyModifierFlags values, repeated here so this stays a plain C++ file.
    constexpr long uiKeyControl   = 1 << 18;
    constexpr long uiKeyAlternate = 1 << 19;
    constexpr long uiKeyCommand   = 1 << 20;

    long latchedUIKitModifiers = 0;
    IMP originalModifierFlagsImp = nullptr;

    long getPatchedModifierFlags (id self, SEL cmd)
    {
        return ((long (*) (id, SEL)) originalModifierFlagsImp) (self, cmd) | latchedUIKitModifiers;
    }

    /*  JUCE rebuilds ModifierKeys::currentModifiers from -[UIEvent modifierFlags] on every
        single touch, so writing to currentModifiers directly is pointless - it is overwritten
        before the event is dispatched. Answering the UIEvent question ourselves is the one
        place a latched on-screen modifier can be injected, and it covers touches and key
        presses alike.
    */
    void setLatchedUIKitModifiers (long flags)
    {
        static const bool patched = []
        {
            if (auto* method = class_getInstanceMethod ((Class) objc_getClass ("UIEvent"),
                                                        sel_registerName ("modifierFlags")))
            {
                originalModifierFlagsImp = method_setImplementation (method, (IMP) getPatchedModifierFlags);
                return true;
            }

            jassertfalse; // -[UIEvent modifierFlags] has moved; the modifier buttons will do nothing
            return false;
        }();

        if (patched)
            latchedUIKitModifiers = flags;
    }
   #endif
}

//==============================================================================
TouchAssistBar::TouchAssistBar()
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);

    auto& commandManager = ProjucerApplication::getCommandManager();

    auto command = [&commandManager] (CommandID id)
    {
        return [&commandManager, id] { commandManager.invokeDirectly (id, false); };
    };

    addButton (undoButton,  command (StandardApplicationCommandIDs::undo),  false);
    addButton (redoButton,  command (StandardApplicationCommandIDs::redo),  false);
    addButton (copyButton,  command (StandardApplicationCommandIDs::copy),  false);
    addButton (pasteButton, command (StandardApplicationCommandIDs::paste), false);

    for (auto* b : { &ctrlButton, &optionButton, &commandButton })
        addModifierButton (*b);

    auto cursorKey = [this] (int keyCode)
    {
        return [this, keyCode] { sendKeyPressToFocusedComponent (KeyPress (keyCode, getLatchedModifiers(), 0)); };
    };

    addButton (leftButton,  cursorKey (KeyPress::leftKey),  true);
    addButton (upButton,    cursorKey (KeyPress::upKey),    true);
    addButton (downButton,  cursorKey (KeyPress::downKey),  true);
    addButton (rightButton, cursorKey (KeyPress::rightKey), true);

    updateLatchedModifiers(); // installs the UIEvent patch now, so a failure shows up at startup
}

TouchAssistBar::~TouchAssistBar()
{
   #if JUCE_IOS
    setLatchedUIKitModifiers (0);
   #endif
}

void TouchAssistBar::addModifierButton (Button& b)
{
    b.setClickingTogglesState (true);
    b.setColour (TextButton::buttonOnColourId, Colours::orange);
    addButton (b, [this] { updateLatchedModifiers(); }, false);
}

ModifierKeys TouchAssistBar::getLatchedModifiers() const
{
    return ModifierKeys ((ctrlButton.getToggleState()    ? ModifierKeys::ctrlModifier    : 0)
                       | (optionButton.getToggleState()  ? ModifierKeys::altModifier     : 0)
                       | (commandButton.getToggleState() ? ModifierKeys::commandModifier : 0));
}

void TouchAssistBar::updateLatchedModifiers()
{
   #if JUCE_IOS
    setLatchedUIKitModifiers ((ctrlButton.getToggleState()    ? uiKeyControl   : 0)
                            | (optionButton.getToggleState()  ? uiKeyAlternate : 0)
                            | (commandButton.getToggleState() ? uiKeyCommand   : 0));
   #endif
}

void TouchAssistBar::addButton (Button& b, std::function<void()> action, bool autoRepeat)
{
    // The bar must never take focus away from the editor it is driving.
    b.setWantsKeyboardFocus (false);
    b.setMouseClickGrabsKeyboardFocus (false);
    b.onClick = std::move (action);

    if (autoRepeat)
        b.setRepeatSpeed (400, 100);

    addAndMakeVisible (b);
}

void TouchAssistBar::paint (Graphics& g)
{
    g.setColour (findColour (secondaryBackgroundColourId));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void TouchAssistBar::resized()
{
    constexpr int gap = 8;

    auto r = getLocalBounds().reduced (4, 4);

    // commands and modifiers on the left, cursor keys on the right, so both ends
    // are reachable by thumb
    auto placeLeft = [&r] (Button& b, int width)
    {
        b.setBounds (r.removeFromLeft (width));
        r.removeFromLeft (gap);
    };

    for (auto* b : { &undoButton, &redoButton, &copyButton, &pasteButton })
        placeLeft (*b, 76);

    for (auto* b : { &ctrlButton, &optionButton, &commandButton })
        placeLeft (*b, 62);

    for (auto* b : { (Button*) &rightButton, (Button*) &downButton, (Button*) &upButton, (Button*) &leftButton })
    {
        b->setBounds (r.removeFromRight (48).reduced (10, 6));
        r.removeFromRight (gap);
    }
}
