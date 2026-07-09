/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2022 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 7 End-User License
   Agreement and JUCE Privacy Policy.

   End User License Agreement: www.juce.com/juce-7-licence
   Privacy Policy: www.juce.com/juce-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

#pragma once


//==============================================================================
/** Editor-side stand-in for juce::MidiKeyboardComponent.

    Projucer itself does not link juce_audio_utils, so the live layout uses this
    lightweight preview while generated code still emits the real
    juce::MidiKeyboardComponent + a paired juce::MidiKeyboardState member named
    "{memberVariableName}State".
*/
class MidiKeyboardComponentPreview  : public Component
{
public:
    enum Orientation
    {
        horizontalKeyboard = 0,
        verticalKeyboardFacingLeft,
        verticalKeyboardFacingRight
    };

    // Match juce::MidiKeyboardComponent::ColourIds so colour property XML/codegen stay compatible.
    enum ColourIds
    {
        whiteNoteColourId           = 0x1005000,
        blackNoteColourId           = 0x1005001,
        keySeparatorLineColourId    = 0x1005002,
        mouseOverKeyOverlayColourId = 0x1005003,
        keyDownOverlayColourId      = 0x1005004,
        textLabelColourId           = 0x1005005,
        shadowColourId              = 0x1005006
    };

    MidiKeyboardComponentPreview()
        : Component ("new midi keyboard")
    {
        setOpaque (true);
    }

    Orientation getKeyboardOrientation() const noexcept   { return orientation; }
    void setKeyboardOrientation (Orientation o) noexcept  { orientation = o; repaint(); }

    int getMidiChannel() const noexcept                   { return midiChannel; }
    void setMidiChannel (int channel) noexcept
    {
        midiChannel = jlimit (1, 16, channel);
    }

    int getMidiChannelsToDisplay() const noexcept         { return midiChannelsToDisplay; }
    void setMidiChannelsToDisplay (int mask) noexcept     { midiChannelsToDisplay = mask; }

    float getVelocity() const noexcept                    { return velocity; }
    bool getUseMousePositionForVelocity() const noexcept  { return useMousePositionForVelocity; }
    void setVelocity (float v, bool useMousePos) noexcept
    {
        velocity = jlimit (0.0f, 1.0f, v);
        useMousePositionForVelocity = useMousePos;
    }

    int getRangeStart() const noexcept                    { return rangeStart; }
    int getRangeEnd() const noexcept                      { return rangeEnd; }
    void setAvailableRange (int lowest, int highest) noexcept
    {
        rangeStart = jlimit (0, 127, lowest);
        rangeEnd   = jlimit (rangeStart, 127, highest);
        repaint();
    }

    float getKeyWidth() const noexcept                    { return keyWidth; }
    void setKeyWidth (float width) noexcept
    {
        keyWidth = jmax (4.0f, width);
        repaint();
    }

    void paint (Graphics& g) override
    {
        // Approximate juce::KeyboardComponentBase / MidiKeyboardComponent geometry so that
        // orientation, keyWidth and available range are visible in the editor preview.
        const auto whiteCol = isColourSpecified (whiteNoteColourId) ? findColour (whiteNoteColourId) : Colours::white;
        const auto blackCol = isColourSpecified (blackNoteColourId) ? findColour (blackNoteColourId) : Colours::black;
        const auto lineCol  = isColourSpecified (keySeparatorLineColourId) ? findColour (keySeparatorLineColourId)
                                                                            : Colours::grey.withAlpha (0.7f);
        const auto shadowCol = isColourSpecified (shadowColourId) ? findColour (shadowColourId)
                                                                  : Colours::black.withAlpha (0.15f);

        g.setColour (whiteCol);
        g.fillAll();

        static constexpr int whiteNoteOffsets[] = { 0, 2, 4, 5, 7, 9, 11 };
        static constexpr int blackNoteOffsets[] = { 1, 3, 6, 8, 10 };

        auto drawKey = [&g] (Rectangle<float> area, Colour fill, Colour outline, bool isBlack)
        {
            if (area.getWidth() <= 0.5f || area.getHeight() <= 0.5f)
                return;

            g.setColour (fill);
            g.fillRect (area);

            g.setColour (outline);
            if (isBlack)
                g.drawRect (area, 0.5f);
            else
                g.drawLine (area.getRight(), area.getY(), area.getRight(), area.getBottom(), 1.0f);
        };

        // White keys first, then black keys on top (same order as KeyboardComponentBase::paint).
        for (int octaveBase = 0; octaveBase < 128; octaveBase += 12)
        {
            for (auto noteOffset : whiteNoteOffsets)
            {
                const int note = octaveBase + noteOffset;
                if (note < rangeStart || note > rangeEnd)
                    continue;

                drawKey (getRectangleForKey (note), whiteCol, lineCol, false);
            }
        }

        for (int octaveBase = 0; octaveBase < 128; octaveBase += 12)
        {
            for (auto noteOffset : blackNoteOffsets)
            {
                const int note = octaveBase + noteOffset;
                if (note < rangeStart || note > rangeEnd)
                    continue;

                drawKey (getRectangleForKey (note), blackCol, blackCol.brighter (0.15f), true);
            }
        }

        // Light edge shadow like MidiKeyboardComponent::drawKeyboardBackground.
        if (! shadowCol.isTransparent())
        {
            const float keyboardExtent = getKeyPos (rangeEnd).getEnd();

            switch (orientation)
            {
                case horizontalKeyboard:
                    g.setGradientFill ({ shadowCol, 0.0f, 0.0f,
                                         shadowCol.withAlpha (0.0f), 0.0f, 5.0f, false });
                    g.fillRect (0.0f, 0.0f, keyboardExtent, 5.0f);
                    break;

                case verticalKeyboardFacingLeft:
                    g.setGradientFill ({ shadowCol, (float) getWidth(), 0.0f,
                                         shadowCol.withAlpha (0.0f), (float) getWidth() - 5.0f, 0.0f, false });
                    g.fillRect ((float) getWidth() - 5.0f, 0.0f, 5.0f, keyboardExtent);
                    break;

                case verticalKeyboardFacingRight:
                    g.setGradientFill ({ shadowCol, 0.0f, 0.0f,
                                         shadowCol.withAlpha (0.0f), 5.0f, 0.0f, false });
                    g.fillRect (0.0f, 0.0f, 5.0f, keyboardExtent);
                    break;
            }
        }

        g.setColour (lineCol);
        g.drawRect (getLocalBounds().toFloat(), 1.0f);
    }

private:
    static bool isBlackNote (int midiNoteNumber) noexcept
    {
        const int n = midiNoteNumber % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    /** Mirrors KeyboardComponentBase::getKeyPosition (default blackNoteWidthRatio = 0.7). */
    Range<float> getKeyPosition (int midiNoteNumber, float targetKeyWidth) const
    {
        constexpr float blackNoteWidthRatio = 0.7f;
        static const float notePos[] = { 0.0f, 1 - blackNoteWidthRatio * 0.6f,
                                         1.0f, 2 - blackNoteWidthRatio * 0.4f,
                                         2.0f,
                                         3.0f, 4 - blackNoteWidthRatio * 0.7f,
                                         4.0f, 5 - blackNoteWidthRatio * 0.5f,
                                         5.0f, 6 - blackNoteWidthRatio * 0.3f,
                                         6.0f };

        const int octave = midiNoteNumber / 12;
        const int note   = midiNoteNumber % 12;
        const float start = (float) octave * 7.0f * targetKeyWidth + notePos[note] * targetKeyWidth;
        const float width = isBlackNote (note) ? blackNoteWidthRatio * targetKeyWidth : targetKeyWidth;
        return { start, start + width };
    }

    Range<float> getKeyPos (int midiNoteNumber) const
    {
        return getKeyPosition (midiNoteNumber, keyWidth)
                 - getKeyPosition (rangeStart, keyWidth).getStart();
    }

    /** Mirrors KeyboardComponentBase::getRectangleForKey for the three orientations. */
    Rectangle<float> getRectangleForKey (int note) const
    {
        const auto pos = getKeyPos (note);
        const float x = pos.getStart();
        const float w = pos.getLength();
        constexpr float blackNoteLengthRatio = 0.7f;

        if (isBlackNote (note))
        {
            const float blackNoteLength = (orientation == horizontalKeyboard
                                               ? (float) getHeight()
                                               : (float) getWidth()) * blackNoteLengthRatio;

            switch (orientation)
            {
                case horizontalKeyboard:
                    return { x, 0.0f, w, blackNoteLength };
                case verticalKeyboardFacingLeft:
                    return { (float) getWidth() - blackNoteLength, x, blackNoteLength, w };
                case verticalKeyboardFacingRight:
                    return { 0.0f, (float) getHeight() - x - w, blackNoteLength, w };
            }
        }
        else
        {
            switch (orientation)
            {
                case horizontalKeyboard:
                    return { x, 0.0f, w, (float) getHeight() };
                case verticalKeyboardFacingLeft:
                    return { 0.0f, x, (float) getWidth(), w };
                case verticalKeyboardFacingRight:
                    return { 0.0f, (float) getHeight() - x - w, (float) getWidth(), w };
            }
        }

        return {};
    }

    Orientation orientation = horizontalKeyboard;
    int midiChannel = 1;
    int midiChannelsToDisplay = 0xffff;
    float velocity = 1.0f;
    bool useMousePositionForVelocity = true;
    int rangeStart = 0;
    int rangeEnd = 127;
    float keyWidth = 16.0f;
};


//==============================================================================
class MidiKeyboardComponentHandler  : public ComponentTypeHandler
{
public:
    MidiKeyboardComponentHandler()
        : ComponentTypeHandler ("Midi Keyboard", "juce::MidiKeyboardComponent",
                                typeid (MidiKeyboardComponentPreview), 600, 80)
    {
        registerColour (MidiKeyboardComponentPreview::whiteNoteColourId,           "white notes",     "whiteNoteCol");
        registerColour (MidiKeyboardComponentPreview::blackNoteColourId,           "black notes",     "blackNoteCol");
        registerColour (MidiKeyboardComponentPreview::keySeparatorLineColourId,    "key separator",   "keySeparatorCol");
        registerColour (MidiKeyboardComponentPreview::mouseOverKeyOverlayColourId, "mouse over",      "mouseOverCol");
        registerColour (MidiKeyboardComponentPreview::keyDownOverlayColourId,      "key down",        "keyDownCol");
        registerColour (MidiKeyboardComponentPreview::textLabelColourId,           "text label",      "textLabelCol");
        registerColour (MidiKeyboardComponentPreview::shadowColourId,              "shadow",          "shadowCol");
    }

    Component* createNewComponent (JucerDocument*) override
    {
        return new MidiKeyboardComponentPreview();
    }

    XmlElement* createXmlFor (Component* comp, const ComponentLayout* layout) override
    {
        auto* k = dynamic_cast<MidiKeyboardComponentPreview*> (comp);
        auto* e = ComponentTypeHandler::createXmlFor (comp, layout);

        if (k == nullptr || e == nullptr)
            return e;

        e->setAttribute ("orientation", orientationToString (k->getKeyboardOrientation()));
        e->setAttribute ("midiChannel", k->getMidiChannel());
        e->setAttribute ("midiChannelsToDisplay", k->getMidiChannelsToDisplay());
        e->setAttribute ("velocity", (double) k->getVelocity());
        e->setAttribute ("useMousePositionForVelocity", k->getUseMousePositionForVelocity());
        e->setAttribute ("rangeStart", k->getRangeStart());
        e->setAttribute ("rangeEnd", k->getRangeEnd());
        e->setAttribute ("keyWidth", (double) k->getKeyWidth());
        e->setAttribute ("needsCallback", needsMidiKeyboardListener (k));

        return e;
    }

    bool restoreFromXml (const XmlElement& xml, Component* comp, const ComponentLayout* layout) override
    {
        if (! ComponentTypeHandler::restoreFromXml (xml, comp, layout))
            return false;

        auto* k = dynamic_cast<MidiKeyboardComponentPreview*> (comp);
        if (k == nullptr)
            return false;

        k->setKeyboardOrientation (stringToOrientation (xml.getStringAttribute ("orientation", "horizontalKeyboard")));
        k->setMidiChannel (xml.getIntAttribute ("midiChannel", 1));
        k->setMidiChannelsToDisplay (xml.getIntAttribute ("midiChannelsToDisplay", 0xffff));
        k->setVelocity ((float) xml.getDoubleAttribute ("velocity", 1.0),
                        xml.getBoolAttribute ("useMousePositionForVelocity", true));
        k->setAvailableRange (xml.getIntAttribute ("rangeStart", 0),
                              xml.getIntAttribute ("rangeEnd", 127));
        k->setKeyWidth ((float) xml.getDoubleAttribute ("keyWidth", 16.0));
        setNeedsMidiKeyboardListener (k, xml.getBoolAttribute ("needsCallback", true));

        return true;
    }

    void getEditableProperties (Component* component, JucerDocument& document,
                                Array<PropertyComponent*>& props, bool multipleSelected) override
    {
        ComponentTypeHandler::getEditableProperties (component, document, props, multipleSelected);

        if (multipleSelected)
            return;

        if (auto* k = dynamic_cast<MidiKeyboardComponentPreview*> (component))
        {
            props.add (new OrientationProperty (k, document));
            props.add (new MidiChannelProperty (k, document));
            props.add (new MidiChannelsToDisplayProperty (k, document));
            props.add (new VelocityProperty (k, document));
            props.add (new UseMouseVelocityProperty (k, document));
            props.add (new RangeStartProperty (k, document));
            props.add (new RangeEndProperty (k, document));
            props.add (new KeyWidthProperty (k, document));
            props.add (new CallbackProperty (k, document));
            addLookAndFeelProperty (k, document, props);
            addColourProperties (k, document, props);
        }
    }

    static String getStateMemberName (const String& memberVariableName)
    {
        return memberVariableName + "State";
    }

    String getCreationParameters (GeneratedCode& code, Component* component) override
    {
        auto* k = dynamic_cast<MidiKeyboardComponentPreview*> (component);
        if (k == nullptr)
            return {};

        const String memberVariableName (code.document->getComponentLayout()->getComponentMemberVariableName (component));
        const String stateName (getStateMemberName (memberVariableName));

        return stateName + ",\njuce::MidiKeyboardComponent::" + orientationToCode (k->getKeyboardOrientation());
    }

    void fillInMemberVariableDeclarations (GeneratedCode& code, Component* component,
                                           const String& memberVariableName) override
    {
        code.privateMemberDeclarations
            << "juce::MidiKeyboardState " << getStateMemberName (memberVariableName) << ";\n";

        ComponentTypeHandler::fillInMemberVariableDeclarations (code, component, memberVariableName);
    }

    void fillInCreationCode (GeneratedCode& code, Component* component,
                             const String& memberVariableName) override
    {
        ComponentTypeHandler::fillInCreationCode (code, component, memberVariableName);

        auto* k = dynamic_cast<MidiKeyboardComponentPreview*> (component);
        if (k == nullptr)
            return;

        const String stateName (getStateMemberName (memberVariableName));

        String r;
        r << memberVariableName << "->setMidiChannel (" << k->getMidiChannel() << ");\n";

        if (k->getMidiChannelsToDisplay() != 0xffff)
            r << memberVariableName << "->setMidiChannelsToDisplay ("
              << k->getMidiChannelsToDisplay() << ");\n";

        if (! approximatelyEqual (k->getVelocity(), 1.0f) || ! k->getUseMousePositionForVelocity())
            r << memberVariableName << "->setVelocity ("
              << CodeHelpers::floatLiteral (k->getVelocity(), 3) << ", "
              << CodeHelpers::boolLiteral (k->getUseMousePositionForVelocity()) << ");\n";

        if (k->getRangeStart() != 0 || k->getRangeEnd() != 127)
            r << memberVariableName << "->setAvailableRange ("
              << k->getRangeStart() << ", " << k->getRangeEnd() << ");\n";

        if (! approximatelyEqual (k->getKeyWidth(), 16.0f))
            r << memberVariableName << "->setKeyWidth ("
              << CodeHelpers::floatLiteral (k->getKeyWidth(), 2) << ");\n";

        r << getColourIntialisationCode (component, memberVariableName);

        if (needsMidiKeyboardListener (component))
            r << stateName << ".addListener (this);\n";

        r << '\n';
        code.constructorCode += r;
    }

    void fillInDeletionCode (GeneratedCode& code, Component* component,
                             const String& memberVariableName) override
    {
        if (needsMidiKeyboardListener (component))
            code.destructorCode
                << getStateMemberName (memberVariableName) << ".removeListener (this);\n";

        ComponentTypeHandler::fillInDeletionCode (code, component, memberVariableName);
    }

    void fillInGeneratedCode (Component* component, GeneratedCode& code) override
    {
        ComponentTypeHandler::fillInGeneratedCode (component, code);

        if (! needsMidiKeyboardListener (component))
            return;

        const String memberVariableName (code.document->getComponentLayout()->getComponentMemberVariableName (component));
        const String stateName (getStateMemberName (memberVariableName));
        const String userCodeComment ("UserMidiKeyboardCode_" + memberVariableName);

        {
            String& callback = code.getCallbackCode ("public juce::MidiKeyboardState::Listener",
                                                     "void",
                                                     "handleNoteOn (juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity)",
                                                     true);

            if (callback.isNotEmpty())
                callback << "else ";

            callback
                << "if (source == &" << stateName << ")\n"
                << "{\n    //[" << userCodeComment << "_NoteOn] -- add your midi keyboard note-on handling code here..\n"
                << "    //[/" << userCodeComment << "_NoteOn]\n}\n";
        }

        {
            String& callback = code.getCallbackCode ("public juce::MidiKeyboardState::Listener",
                                                     "void",
                                                     "handleNoteOff (juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity)",
                                                     true);

            if (callback.isNotEmpty())
                callback << "else ";

            callback
                << "if (source == &" << stateName << ")\n"
                << "{\n    //[" << userCodeComment << "_NoteOff] -- add your midi keyboard note-off handling code here..\n"
                << "    //[/" << userCodeComment << "_NoteOff]\n}\n";
        }
    }

    static bool needsMidiKeyboardListener (Component* c)
    {
        return c->getProperties().getWithDefault ("generateListenerCallback", true);
    }

    static void setNeedsMidiKeyboardListener (Component* c, bool shouldDoCallback)
    {
        c->getProperties().set ("generateListenerCallback", shouldDoCallback);
    }

private:
    static String orientationToString (MidiKeyboardComponentPreview::Orientation o)
    {
        switch (o)
        {
            case MidiKeyboardComponentPreview::verticalKeyboardFacingLeft:  return "verticalKeyboardFacingLeft";
            case MidiKeyboardComponentPreview::verticalKeyboardFacingRight: return "verticalKeyboardFacingRight";
            case MidiKeyboardComponentPreview::horizontalKeyboard:
            default:                                                        return "horizontalKeyboard";
        }
    }

    static String orientationToCode (MidiKeyboardComponentPreview::Orientation o)
    {
        return orientationToString (o);
    }

    static MidiKeyboardComponentPreview::Orientation stringToOrientation (const String& s)
    {
        if (s == "verticalKeyboardFacingLeft")  return MidiKeyboardComponentPreview::verticalKeyboardFacingLeft;
        if (s == "verticalKeyboardFacingRight") return MidiKeyboardComponentPreview::verticalKeyboardFacingRight;
        return MidiKeyboardComponentPreview::horizontalKeyboard;
    }

    //==============================================================================
    struct OrientationProperty  : public ComponentChoiceProperty<MidiKeyboardComponentPreview>
    {
        OrientationProperty (MidiKeyboardComponentPreview* c, JucerDocument& doc)
            : ComponentChoiceProperty<MidiKeyboardComponentPreview> ("orientation", c, doc)
        {
            choices.add ("Horizontal");
            choices.add ("Vertical facing left");
            choices.add ("Vertical facing right");
        }

        void setIndex (int newIndex) override
        {
            auto o = MidiKeyboardComponentPreview::horizontalKeyboard;
            if (newIndex == 1) o = MidiKeyboardComponentPreview::verticalKeyboardFacingLeft;
            if (newIndex == 2) o = MidiKeyboardComponentPreview::verticalKeyboardFacingRight;

            document.perform (new OrientationChangeAction (component, *document.getComponentLayout(), o),
                              "Change midi keyboard orientation");
        }

        int getIndex() const override
        {
            return (int) component->getKeyboardOrientation();
        }

        struct OrientationChangeAction  : public ComponentUndoableAction<MidiKeyboardComponentPreview>
        {
            OrientationChangeAction (MidiKeyboardComponentPreview* comp, ComponentLayout& l,
                                     MidiKeyboardComponentPreview::Orientation newState_)
                : ComponentUndoableAction<MidiKeyboardComponentPreview> (comp, l),
                  newState (newState_),
                  oldState (comp->getKeyboardOrientation())
            {}

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setKeyboardOrientation (newState);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setKeyboardOrientation (oldState);
                changed();
                return true;
            }

            MidiKeyboardComponentPreview::Orientation newState, oldState;
        };
    };

    //==============================================================================
    struct MidiChannelProperty  : public ComponentTextProperty<MidiKeyboardComponentPreview>
    {
        MidiChannelProperty (MidiKeyboardComponentPreview* c, JucerDocument& doc)
            : ComponentTextProperty<MidiKeyboardComponentPreview> ("midi channel", 3, false, c, doc)
        {}

        void setText (const String& newText) override
        {
            document.perform (new ChannelChangeAction (component, *document.getComponentLayout(),
                                                       jlimit (1, 16, newText.getIntValue())),
                              "Change midi keyboard channel");
        }

        String getText() const override
        {
            return String (component->getMidiChannel());
        }

        struct ChannelChangeAction  : public ComponentUndoableAction<MidiKeyboardComponentPreview>
        {
            ChannelChangeAction (MidiKeyboardComponentPreview* comp, ComponentLayout& l, int newState_)
                : ComponentUndoableAction<MidiKeyboardComponentPreview> (comp, l),
                  newState (newState_),
                  oldState (comp->getMidiChannel())
            {}

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setMidiChannel (newState);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setMidiChannel (oldState);
                changed();
                return true;
            }

            int newState, oldState;
        };
    };

    //==============================================================================
    struct MidiChannelsToDisplayProperty  : public ComponentTextProperty<MidiKeyboardComponentPreview>
    {
        MidiChannelsToDisplayProperty (MidiKeyboardComponentPreview* c, JucerDocument& doc)
            : ComponentTextProperty<MidiKeyboardComponentPreview> ("display channels mask", 8, false, c, doc)
        {}

        void setText (const String& newText) override
        {
            const int mask = newText.startsWithIgnoreCase ("0x") ? newText.getHexValue32()
                                                                 : newText.getIntValue();
            document.perform (new MaskChangeAction (component, *document.getComponentLayout(), mask),
                              "Change midi keyboard display channels");
        }

        String getText() const override
        {
            return "0x" + String::toHexString (component->getMidiChannelsToDisplay());
        }

        struct MaskChangeAction  : public ComponentUndoableAction<MidiKeyboardComponentPreview>
        {
            MaskChangeAction (MidiKeyboardComponentPreview* comp, ComponentLayout& l, int newState_)
                : ComponentUndoableAction<MidiKeyboardComponentPreview> (comp, l),
                  newState (newState_),
                  oldState (comp->getMidiChannelsToDisplay())
            {}

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setMidiChannelsToDisplay (newState);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setMidiChannelsToDisplay (oldState);
                changed();
                return true;
            }

            int newState, oldState;
        };
    };

    //==============================================================================
    struct VelocityProperty  : public ComponentTextProperty<MidiKeyboardComponentPreview>
    {
        VelocityProperty (MidiKeyboardComponentPreview* c, JucerDocument& doc)
            : ComponentTextProperty<MidiKeyboardComponentPreview> ("velocity", 8, false, c, doc)
        {}

        void setText (const String& newText) override
        {
            document.perform (new VelocityChangeAction (component, *document.getComponentLayout(),
                                                        (float) newText.getDoubleValue()),
                              "Change midi keyboard velocity");
        }

        String getText() const override
        {
            return String (component->getVelocity(), 3);
        }

        struct VelocityChangeAction  : public ComponentUndoableAction<MidiKeyboardComponentPreview>
        {
            VelocityChangeAction (MidiKeyboardComponentPreview* comp, ComponentLayout& l, float newState_)
                : ComponentUndoableAction<MidiKeyboardComponentPreview> (comp, l),
                  newState (newState_),
                  oldState (comp->getVelocity())
            {}

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setVelocity (newState, getComponent()->getUseMousePositionForVelocity());
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setVelocity (oldState, getComponent()->getUseMousePositionForVelocity());
                changed();
                return true;
            }

            float newState, oldState;
        };
    };

    //==============================================================================
    struct UseMouseVelocityProperty  : public ComponentBooleanProperty<MidiKeyboardComponentPreview>
    {
        UseMouseVelocityProperty (MidiKeyboardComponentPreview* c, JucerDocument& doc)
            : ComponentBooleanProperty<MidiKeyboardComponentPreview> ("mouse velocity",
                                                                      "Use mouse position for velocity",
                                                                      "Use mouse position for velocity",
                                                                      c, doc)
        {}

        void setState (bool newState) override
        {
            document.perform (new MouseVelChangeAction (component, *document.getComponentLayout(), newState),
                              "Change midi keyboard mouse velocity mode");
        }

        bool getState() const override
        {
            return component->getUseMousePositionForVelocity();
        }

        struct MouseVelChangeAction  : public ComponentUndoableAction<MidiKeyboardComponentPreview>
        {
            MouseVelChangeAction (MidiKeyboardComponentPreview* comp, ComponentLayout& l, bool newState_)
                : ComponentUndoableAction<MidiKeyboardComponentPreview> (comp, l),
                  newState (newState_),
                  oldState (comp->getUseMousePositionForVelocity())
            {}

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setVelocity (getComponent()->getVelocity(), newState);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setVelocity (getComponent()->getVelocity(), oldState);
                changed();
                return true;
            }

            bool newState, oldState;
        };
    };

    //==============================================================================
    struct RangeStartProperty  : public ComponentTextProperty<MidiKeyboardComponentPreview>
    {
        RangeStartProperty (MidiKeyboardComponentPreview* c, JucerDocument& doc)
            : ComponentTextProperty<MidiKeyboardComponentPreview> ("range start", 4, false, c, doc)
        {}

        void setText (const String& newText) override
        {
            document.perform (new RangeChangeAction (component, *document.getComponentLayout(),
                                                     newText.getIntValue(), component->getRangeEnd()),
                              "Change midi keyboard range start");
        }

        String getText() const override { return String (component->getRangeStart()); }

        struct RangeChangeAction  : public ComponentUndoableAction<MidiKeyboardComponentPreview>
        {
            RangeChangeAction (MidiKeyboardComponentPreview* comp, ComponentLayout& l, int newStart_, int newEnd_)
                : ComponentUndoableAction<MidiKeyboardComponentPreview> (comp, l),
                  newStart (newStart_), newEnd (newEnd_),
                  oldStart (comp->getRangeStart()), oldEnd (comp->getRangeEnd())
            {}

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setAvailableRange (newStart, newEnd);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setAvailableRange (oldStart, oldEnd);
                changed();
                return true;
            }

            int newStart, newEnd, oldStart, oldEnd;
        };
    };

    struct RangeEndProperty  : public ComponentTextProperty<MidiKeyboardComponentPreview>
    {
        RangeEndProperty (MidiKeyboardComponentPreview* c, JucerDocument& doc)
            : ComponentTextProperty<MidiKeyboardComponentPreview> ("range end", 4, false, c, doc)
        {}

        void setText (const String& newText) override
        {
            document.perform (new RangeStartProperty::RangeChangeAction (component, *document.getComponentLayout(),
                                                                         component->getRangeStart(), newText.getIntValue()),
                              "Change midi keyboard range end");
        }

        String getText() const override { return String (component->getRangeEnd()); }
    };

    //==============================================================================
    struct KeyWidthProperty  : public ComponentTextProperty<MidiKeyboardComponentPreview>
    {
        KeyWidthProperty (MidiKeyboardComponentPreview* c, JucerDocument& doc)
            : ComponentTextProperty<MidiKeyboardComponentPreview> ("key width", 8, false, c, doc)
        {}

        void setText (const String& newText) override
        {
            document.perform (new KeyWidthChangeAction (component, *document.getComponentLayout(),
                                                        (float) newText.getDoubleValue()),
                              "Change midi keyboard key width");
        }

        String getText() const override
        {
            return String (component->getKeyWidth(), 2);
        }

        struct KeyWidthChangeAction  : public ComponentUndoableAction<MidiKeyboardComponentPreview>
        {
            KeyWidthChangeAction (MidiKeyboardComponentPreview* comp, ComponentLayout& l, float newState_)
                : ComponentUndoableAction<MidiKeyboardComponentPreview> (comp, l),
                  newState (newState_),
                  oldState (comp->getKeyWidth())
            {}

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setKeyWidth (newState);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setKeyWidth (oldState);
                changed();
                return true;
            }

            float newState, oldState;
        };
    };

    //==============================================================================
    struct CallbackProperty  : public ComponentBooleanProperty<MidiKeyboardComponentPreview>
    {
        CallbackProperty (MidiKeyboardComponentPreview* c, JucerDocument& doc)
            : ComponentBooleanProperty<MidiKeyboardComponentPreview> ("callback",
                                                                      "Generate MidiKeyboardState::Listener",
                                                                      "Generate MidiKeyboardState::Listener",
                                                                      c, doc)
        {}

        void setState (bool newState) override
        {
            document.perform (new CallbackChangeAction (component, *document.getComponentLayout(), newState),
                              "Change midi keyboard callback");
        }

        bool getState() const override
        {
            return needsMidiKeyboardListener (component);
        }

        struct CallbackChangeAction  : public ComponentUndoableAction<MidiKeyboardComponentPreview>
        {
            CallbackChangeAction (MidiKeyboardComponentPreview* comp, ComponentLayout& l, bool newState_)
                : ComponentUndoableAction<MidiKeyboardComponentPreview> (comp, l),
                  newState (newState_),
                  oldState (needsMidiKeyboardListener (comp))
            {}

            bool perform() override
            {
                showCorrectTab();
                setNeedsMidiKeyboardListener (getComponent(), newState);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                setNeedsMidiKeyboardListener (getComponent(), oldState);
                changed();
                return true;
            }

            bool newState, oldState;
        };
    };
};
