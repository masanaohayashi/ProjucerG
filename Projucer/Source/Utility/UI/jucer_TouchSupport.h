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


#pragma once

//==============================================================================
/**
    Touch devices have no second mouse button, so a long press is turned into a
    synthetic right-click on whichever component is under the finger.

    Create one of these once at startup and it will apply application-wide.
*/
class LongPressToRightClick final : private MouseListener,
                                    private Timer
{
public:
    LongPressToRightClick()            { Desktop::getInstance().addGlobalMouseListener (this); }
    ~LongPressToRightClick() override  { Desktop::getInstance().removeGlobalMouseListener (this); }

private:
    static constexpr int holdTimeMs = 500;

    void mouseDown (const MouseEvent& e) override
    {
        if (! e.source.isTouch() || e.mods.isPopupMenu())
            return;

        target = e.eventComponent;
        screenPosition = e.getScreenPosition().toFloat();
        sourceIndex = e.source.getIndex();
        startTimer (holdTimeMs);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (e.source.hasMouseMovedSignificantlySincePressed())
            stopTimer();
    }

    void mouseUp (const MouseEvent&) override  { stopTimer(); }

    void timerCallback() override
    {
        stopTimer();

        auto* comp = target.getComponent();
        auto* source = Desktop::getInstance().getMouseSource (sourceIndex);

        if (comp == nullptr || source == nullptr)
            return;

        const ModifierKeys mods (ModifierKeys::rightButtonModifier | ModifierKeys::popupMenuClickModifier);
        const ScopedValueSetter<ModifierKeys> scope (ModifierKeys::currentModifiers, mods);

        const auto local = comp->getLocalPoint (nullptr, screenPosition);
        const auto now = Time::getCurrentTime();

        comp->mouseDown ({ *source, local, mods,
                           MouseInputSource::defaultPressure,
                           MouseInputSource::defaultOrientation, MouseInputSource::defaultRotation,
                           MouseInputSource::defaultTiltX, MouseInputSource::defaultTiltY,
                           comp, comp, now, local, now, 1, false });
    }

    Component::SafePointer<Component> target;
    Point<float> screenPosition;
    int sourceIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LongPressToRightClick)
};

//==============================================================================
/**
    An always-visible strip of buttons providing the things a touch-only device
    can't otherwise reach: undo, redo, the modifier keys, and the cursor keys.

    The modifier buttons latch: tap one to hold it down, tap again to release. While
    latched it applies to real touches too, so things like option-drag work.
*/
class TouchAssistBar final : public Component
{
public:
    TouchAssistBar();
    ~TouchAssistBar() override;

    static constexpr int barHeight = 44;

    void resized() override;
    void paint (Graphics&) override;

private:
    void addButton (Button&, std::function<void()> action, bool autoRepeat);
    void addModifierButton (Button&);
    void updateLatchedModifiers();
    ModifierKeys getLatchedModifiers() const;

    TextButton undoButton { "Undo" }, redoButton { "Redo" };
    TextButton copyButton { "Copy" }, pasteButton { "Paste" };
    TextButton ctrlButton { "Ctrl" }, optionButton { "Opt" }, commandButton { "Cmd" };
    // ArrowButton's direction is a fraction of a full turn, starting from "pointing right".
    ArrowButton leftButton  { "left",  0.5f,  Colours::white },
                upButton    { "up",    0.75f, Colours::white },
                downButton  { "down",  0.25f, Colours::white },
                rightButton { "right", 0.0f,  Colours::white };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchAssistBar)
};
