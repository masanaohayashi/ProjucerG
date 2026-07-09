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
/** Holds MidiKeyboardState so it is constructed before MidiKeyboardComponent. */
struct MidiKeyboardStateHolder
{
    juce::MidiKeyboardState keyboardState;
};

/** Editor-side MidiKeyboardComponent with its own MidiKeyboardState.

    Same idea as DemoTreeView: the live layout uses a real component, while
    generated code still emits juce::MidiKeyboardComponent plus a separate
    "{memberVariableName}State" member.
*/
class MidiKeyboardComponentPreview  : private MidiKeyboardStateHolder,
                                      public juce::MidiKeyboardComponent
{
public:
    MidiKeyboardComponentPreview()
        : juce::MidiKeyboardComponent (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        setName ("new midi keyboard");
    }

    float getVelocitySetting() const noexcept                   { return velocitySetting; }
    bool getUseMousePositionForVelocity() const noexcept        { return useMousePositionForVelocitySetting; }

    void setVelocitySetting (float v, bool useMousePos) noexcept
    {
        velocitySetting = jlimit (0.0f, 1.0f, v);
        useMousePositionForVelocitySetting = useMousePos;
        setVelocity (velocitySetting, useMousePositionForVelocitySetting);
    }

private:
    // MidiKeyboardComponent does not expose getters for these; keep copies for XML/codegen.
    float velocitySetting = 1.0f;
    bool useMousePositionForVelocitySetting = true;
};


//==============================================================================
class MidiKeyboardComponentHandler  : public ComponentTypeHandler
{
public:
    MidiKeyboardComponentHandler()
        : ComponentTypeHandler ("Midi Keyboard", "juce::MidiKeyboardComponent",
                                typeid (MidiKeyboardComponentPreview), 600, 80)
    {
        registerColour (juce::MidiKeyboardComponent::whiteNoteColourId,           "white notes",     "whiteNoteCol");
        registerColour (juce::MidiKeyboardComponent::blackNoteColourId,           "black notes",     "blackNoteCol");
        registerColour (juce::MidiKeyboardComponent::keySeparatorLineColourId,    "key separator",   "keySeparatorCol");
        registerColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, "mouse over",      "mouseOverCol");
        registerColour (juce::MidiKeyboardComponent::keyDownOverlayColourId,      "key down",        "keyDownCol");
        registerColour (juce::MidiKeyboardComponent::textLabelColourId,           "text label",      "textLabelCol");
        registerColour (juce::MidiKeyboardComponent::shadowColourId,              "shadow",          "shadowCol");
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

        e->setAttribute ("orientation", orientationToString (k->getOrientation()));
        e->setAttribute ("midiChannel", k->getMidiChannel());
        e->setAttribute ("midiChannelsToDisplay", k->getMidiChannelsToDisplay());
        e->setAttribute ("velocity", (double) k->getVelocitySetting());
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

        k->setOrientation (stringToOrientation (xml.getStringAttribute ("orientation", "horizontalKeyboard")));
        k->setMidiChannel (xml.getIntAttribute ("midiChannel", 1));
        k->setMidiChannelsToDisplay (xml.getIntAttribute ("midiChannelsToDisplay", 0xffff));
        k->setVelocitySetting ((float) xml.getDoubleAttribute ("velocity", 1.0),
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

        return stateName + ",\njuce::MidiKeyboardComponent::" + orientationToCode (k->getOrientation());
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

        if (! approximatelyEqual (k->getVelocitySetting(), 1.0f) || ! k->getUseMousePositionForVelocity())
            r << memberVariableName << "->setVelocity ("
              << CodeHelpers::floatLiteral (k->getVelocitySetting(), 3) << ", "
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
    using Orientation = juce::MidiKeyboardComponent::Orientation;

    static String orientationToString (Orientation o)
    {
        switch (o)
        {
            case juce::MidiKeyboardComponent::verticalKeyboardFacingLeft:  return "verticalKeyboardFacingLeft";
            case juce::MidiKeyboardComponent::verticalKeyboardFacingRight: return "verticalKeyboardFacingRight";
            case juce::MidiKeyboardComponent::horizontalKeyboard:
            default:                                                       return "horizontalKeyboard";
        }
    }

    static String orientationToCode (Orientation o)
    {
        return orientationToString (o);
    }

    static Orientation stringToOrientation (const String& s)
    {
        if (s == "verticalKeyboardFacingLeft")  return juce::MidiKeyboardComponent::verticalKeyboardFacingLeft;
        if (s == "verticalKeyboardFacingRight") return juce::MidiKeyboardComponent::verticalKeyboardFacingRight;
        return juce::MidiKeyboardComponent::horizontalKeyboard;
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
            auto o = juce::MidiKeyboardComponent::horizontalKeyboard;
            if (newIndex == 1) o = juce::MidiKeyboardComponent::verticalKeyboardFacingLeft;
            if (newIndex == 2) o = juce::MidiKeyboardComponent::verticalKeyboardFacingRight;

            document.perform (new OrientationChangeAction (component, *document.getComponentLayout(), o),
                              "Change midi keyboard orientation");
        }

        int getIndex() const override
        {
            return (int) component->getOrientation();
        }

        struct OrientationChangeAction  : public ComponentUndoableAction<MidiKeyboardComponentPreview>
        {
            OrientationChangeAction (MidiKeyboardComponentPreview* comp, ComponentLayout& l, Orientation newState_)
                : ComponentUndoableAction<MidiKeyboardComponentPreview> (comp, l),
                  newState (newState_),
                  oldState (comp->getOrientation())
            {}

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setOrientation (newState);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setOrientation (oldState);
                changed();
                return true;
            }

            Orientation newState, oldState;
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
            return String (component->getVelocitySetting(), 3);
        }

        struct VelocityChangeAction  : public ComponentUndoableAction<MidiKeyboardComponentPreview>
        {
            VelocityChangeAction (MidiKeyboardComponentPreview* comp, ComponentLayout& l, float newState_)
                : ComponentUndoableAction<MidiKeyboardComponentPreview> (comp, l),
                  newState (newState_),
                  oldState (comp->getVelocitySetting())
            {}

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setVelocitySetting (newState, getComponent()->getUseMousePositionForVelocity());
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setVelocitySetting (oldState, getComponent()->getUseMousePositionForVelocity());
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
                getComponent()->setVelocitySetting (getComponent()->getVelocitySetting(), newState);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setVelocitySetting (getComponent()->getVelocitySetting(), oldState);
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
                getComponent()->setKeyWidth (jmax (4.0f, newState));
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
