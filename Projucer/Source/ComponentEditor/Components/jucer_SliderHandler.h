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
static const Slider::SliderStyle sliderStyleTypes[] =
{
    Slider::LinearHorizontal,
    Slider::LinearVertical,
    Slider::LinearBar,
    Slider::LinearBarVertical,
    Slider::Rotary,
    Slider::RotaryHorizontalDrag,
    Slider::RotaryVerticalDrag,
    Slider::RotaryHorizontalVerticalDrag,
    Slider::IncDecButtons,
    Slider::TwoValueHorizontal,
    Slider::TwoValueVertical,
    Slider::ThreeValueHorizontal,
    Slider::ThreeValueVertical
};

static const Slider::TextEntryBoxPosition sliderTextBoxPositions[] =
{
    Slider::NoTextBox,
    Slider::TextBoxLeft,
    Slider::TextBoxRight,
    Slider::TextBoxAbove,
    Slider::TextBoxBelow
};


struct SliderHandler  : public ComponentTypeHandler
{
    SliderHandler()
        : ComponentTypeHandler ("Slider", "juce::Slider", typeid (Slider), 150, 24)
    {
        registerColour (juce::Slider::backgroundColourId, "background", "bkgcol");
        registerColour (juce::Slider::thumbColourId, "thumb", "thumbcol");
        registerColour (juce::Slider::trackColourId, "track", "trackcol");
        registerColour (juce::Slider::rotarySliderFillColourId, "rotary fill", "rotarysliderfill");
        registerColour (juce::Slider::rotarySliderOutlineColourId, "rotary outln", "rotaryslideroutline");
        registerColour (juce::Slider::textBoxTextColourId, "textbox text", "textboxtext");
        registerColour (juce::Slider::textBoxBackgroundColourId, "textbox bkgd", "textboxbkgd");
        registerColour (juce::Slider::textBoxHighlightColourId, "textbox highlt", "textboxhighlight");
        registerColour (juce::Slider::textBoxOutlineColourId, "textbox outln", "textboxoutline");
    }

    Component* createNewComponent (JucerDocument*) override
    {
        return new Slider ("new slider");
    }

    XmlElement* createXmlFor (Component* comp, const ComponentLayout* layout) override
    {
        XmlElement* e = ComponentTypeHandler::createXmlFor (comp, layout);

        Slider* s = dynamic_cast<Slider*> (comp);
        e->setAttribute ("min", s->getMinimum());
        e->setAttribute ("max", s->getMaximum());
        e->setAttribute ("int", s->getInterval());
        e->setAttribute ("style", sliderStyleToString (s->getSliderStyle()));
        e->setAttribute ("textBoxPos", textBoxPosToString (s->getTextBoxPosition()));
        e->setAttribute ("textBoxEditable", s->isTextBoxEditable());
        e->setAttribute ("textBoxWidth", s->getTextBoxWidth());
        e->setAttribute ("textBoxHeight", s->getTextBoxHeight());
        e->setAttribute ("skewFactor", s->getSkewFactor());
        e->setAttribute ("needsCallback", needsSliderListener (s));
        e->setAttribute ("filmstripImage", getFilmstripImage (s));
        e->setAttribute ("filmstripFrames", getFilmstripFrames (s));
        e->setAttribute ("filmstripVertical", isFilmstripVertical (s));

        return e;
    }

    bool restoreFromXml (const XmlElement& xml, Component* comp, const ComponentLayout* layout) override
    {
        if (! ComponentTypeHandler::restoreFromXml (xml, comp, layout))
            return false;

        Slider* const s = dynamic_cast<Slider*> (comp);

        s->setRange (xml.getDoubleAttribute ("min", 0.0),
                     xml.getDoubleAttribute ("max", 10.0),
                     xml.getDoubleAttribute ("int", 0.0));

        s->setSliderStyle (sliderStringToStyle (xml.getStringAttribute ("style", "LinearHorizontal")));

        s->setTextBoxStyle (stringToTextBoxPos (xml.getStringAttribute ("textBoxPos", "TextBoxLeft")),
                            ! xml.getBoolAttribute ("textBoxEditable", true),
                            xml.getIntAttribute ("textBoxWidth", 80),
                            xml.getIntAttribute ("textBoxHeight", 20));

        s->setSkewFactor (xml.getDoubleAttribute ("skewFactor", 1.0));

        setNeedsSliderListener (s, xml.getBoolAttribute ("needsCallback", true));
        setFilmstripImage (const_cast<ComponentLayout&> (*layout), s, xml.getStringAttribute ("filmstripImage", String()), false);
        setFilmstripFrames (const_cast<ComponentLayout&> (*layout), s, xml.getIntAttribute ("filmstripFrames", 1), false);
        setFilmstripVertical (const_cast<ComponentLayout&> (*layout), s, xml.getBoolAttribute ("filmstripVertical", true), false);

        return true;
    }

    String getCreationParameters (GeneratedCode&, Component* component) override
    {
        return quotedString (component->getName(), false);
    }

    void fillInCreationCode (GeneratedCode& code, Component* component, const String& memberVariableName) override
    {
        ComponentTypeHandler::fillInCreationCode (code, component, memberVariableName);

        Slider* const s = dynamic_cast<Slider*> (component);

        String r;
        r << memberVariableName << "->setRange ("
          << s->getMinimum() << ", " << s->getMaximum() << ", " << s->getInterval()
          << ");\n"
          << memberVariableName << "->setSliderStyle (juce::Slider::"
          << sliderStyleToString (s->getSliderStyle()) << ");\n"
          << memberVariableName << "->setTextBoxStyle (juce::Slider::"
          << textBoxPosToString (s->getTextBoxPosition())
          << ", " << CodeHelpers::boolLiteral (! s->isTextBoxEditable())
          << ", " << s->getTextBoxWidth() << ", " << s->getTextBoxHeight() << ");\n"
          << getColourIntialisationCode (component, memberVariableName);

        if (needsSliderListener (component))
            r << memberVariableName << "->addListener (this);\n";

        if (! approximatelyEqual (s->getSkewFactor(), 1.0))
            r << memberVariableName << "->setSkewFactor (" << s->getSkewFactor() << ");\n";

        addFilmstripCreationCode (code, s, memberVariableName, r);

        r << '\n';
        code.constructorCode += r;
    }

    void fillInGeneratedCode (Component* component, GeneratedCode& code) override
    {
        ComponentTypeHandler::fillInGeneratedCode (component, code);

        if (needsSliderListener (component))
        {
            String& callback = code.getCallbackCode ("public juce::Slider::Listener",
                                                     "void",
                                                     "sliderValueChanged (juce::Slider* sliderThatWasMoved)",
                                                     true);

            if (callback.isNotEmpty())
                callback << "else ";

            const String memberVariableName (code.document->getComponentLayout()->getComponentMemberVariableName (component));
            const String userCodeComment ("UserSliderCode_" + memberVariableName);

            callback
                << "if (sliderThatWasMoved == " << memberVariableName << ".get())\n"
                << "{\n    //[" << userCodeComment << "] -- add your slider handling code here..\n    //[/" << userCodeComment << "]\n}\n";
        }
    }

    void getEditableProperties (Component* component, JucerDocument& document,
                                Array<PropertyComponent*>& props, bool multipleSelected) override
    {
        ComponentTypeHandler::getEditableProperties (component, document, props, multipleSelected);

        if (multipleSelected)
            return;

        if (auto* s = dynamic_cast<Slider*> (component))
        {
            props.add (new SliderRangeProperty (s, document, "minimum", 0));
            props.add (new SliderRangeProperty (s, document, "maximum", 1));
            props.add (new SliderRangeProperty (s, document, "interval", 2));
            props.add (new SliderTypeProperty (s, document));
            props.add (new SliderTextboxProperty (s, document));
            props.add (new SliderTextboxEditableProperty (s, document));
            props.add (new SliderTextboxSizeProperty (s, document, true));
            props.add (new SliderTextboxSizeProperty (s, document, false));
            props.add (new SliderSkewProperty (s, document));
            props.add (new FilmstripImageProperty (*document.getComponentLayout(), s));
            props.add (new FilmstripFramesProperty (*document.getComponentLayout(), s));
            props.add (new FilmstripLayoutProperty (*document.getComponentLayout(), s));
            addLookAndFeelProperty (component, document, props);
            props.add (new SliderCallbackProperty (s, document));
        }

        addColourProperties (component, document, props);
    }

    static bool needsSliderListener (Component* slider)
    {
        return slider->getProperties().getWithDefault ("generateListenerCallback", true);
    }

    static void setNeedsSliderListener (Component* slider, bool shouldDoCallback)
    {
        slider->getProperties().set ("generateListenerCallback", shouldDoCallback);
    }

    bool shouldGenerateLookAndFeelCode (Component* component) const override
    {
        if (auto* slider = dynamic_cast<Slider*> (component))
            return getFilmstripImage (slider).isEmpty();

        return true;
    }

private:
    static Identifier filmstripImageProperty()     { return "filmstripImage"; }
    static Identifier filmstripFramesProperty()    { return "filmstripFrames"; }
    static Identifier filmstripVerticalProperty()  { return "filmstripVertical"; }

    struct FilmstripSliderLookAndFeel  : public LookAndFeel_V4
    {
        Component::SafePointer<Slider> owner;
        Image image;
        String imageResource;
        int frames = 1;
        bool vertical = true;

        void drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float rotaryStartAngle,
                               float rotaryEndAngle, Slider& slider) override
        {
            if (image.isValid() && frames > 1)
            {
                drawFilmstripFrame (g, image, x, y, width, height, sliderPos, frames, vertical);
                return;
            }

            LookAndFeel_V4::drawRotarySlider (g, x, y, width, height, sliderPos,
                                              rotaryStartAngle, rotaryEndAngle, slider);
        }
    };

    static OwnedArray<FilmstripSliderLookAndFeel>& getPreviewLookAndFeels()
    {
        static OwnedArray<FilmstripSliderLookAndFeel> lookAndFeels;
        return lookAndFeels;
    }

    static FilmstripSliderLookAndFeel* getPreviewLookAndFeelFor (Slider& slider)
    {
        auto& lookAndFeels = getPreviewLookAndFeels();

        for (int i = lookAndFeels.size(); --i >= 0;)
        {
            if (lookAndFeels[i]->owner == nullptr)
                lookAndFeels.remove (i);
            else if (lookAndFeels[i]->owner == &slider)
                return lookAndFeels[i];
        }

        auto* lookAndFeel = new FilmstripSliderLookAndFeel();
        lookAndFeel->owner = &slider;
        lookAndFeels.add (lookAndFeel);
        return lookAndFeel;
    }

    static void drawFilmstripFrame (Graphics& g, const Image& image, int x, int y, int width, int height,
                                    float sliderPos, int frames, bool vertical)
    {
        frames = jmax (1, frames);
        const auto frame = jlimit (0, frames - 1, roundToInt (sliderPos * (float) (frames - 1)));

        if (vertical)
        {
            const auto frameHeight = image.getHeight() / frames;

            if (frameHeight > 0)
                g.drawImage (image, x, y, width, height,
                             0, frame * frameHeight, image.getWidth(), frameHeight);
        }
        else
        {
            const auto frameWidth = image.getWidth() / frames;

            if (frameWidth > 0)
                g.drawImage (image, x, y, width, height,
                             frame * frameWidth, 0, frameWidth, image.getHeight());
        }
    }

    static Image getImageForPreview (JucerDocument& document, const String& resourceName)
    {
        if (resourceName.contains ("::"))
        {
            if (auto* project = document.getCppDocument().getProject())
            {
                JucerResourceFile resourceFile (*project);

                for (int i = 0; i < resourceFile.getNumFiles(); ++i)
                {
                    const auto& file = resourceFile.getFile (i);

                    if (resourceName == resourceFile.getClassName() + "::" + resourceFile.getDataVariableFor (file))
                        return ImageCache::getFromFile (file);
                }
            }

            return {};
        }

        return document.getResources().getImageFromCache (resourceName);
    }

    static void updateFilmstripPreview (ComponentLayout& layout, Slider* slider)
    {
        const auto imageResource = getFilmstripImage (slider);

        if (imageResource.isEmpty())
        {
            ComponentTypeHandler::setComponentLookAndFeelString (slider, ComponentTypeHandler::getComponentLookAndFeelString (slider));
            slider->repaint();
            return;
        }

        auto* lookAndFeel = getPreviewLookAndFeelFor (*slider);
        lookAndFeel->imageResource = imageResource;
        lookAndFeel->image = getImageForPreview (*layout.getDocument(), imageResource);
        lookAndFeel->frames = getFilmstripFrames (slider);
        lookAndFeel->vertical = isFilmstripVertical (slider);
        slider->setLookAndFeel (lookAndFeel);
        slider->repaint();
    }

    static String getFilmstripImage (Slider* slider)
    {
        return slider->getProperties()[filmstripImageProperty()].toString();
    }

    static int getFilmstripFrames (Slider* slider)
    {
        return jmax (1, (int) slider->getProperties().getWithDefault (filmstripFramesProperty(), 1));
    }

    static bool isFilmstripVertical (Slider* slider)
    {
        return slider->getProperties().getWithDefault (filmstripVerticalProperty(), true);
    }

    struct SetFilmstripPropertyAction  : public ComponentUndoableAction<Slider>
    {
        SetFilmstripPropertyAction (Slider* slider, ComponentLayout& l, Identifier property_, var newValue_)
            : ComponentUndoableAction<Slider> (slider, l),
              property (property_),
              newValue (std::move (newValue_))
        {
            oldValue = slider->getProperties()[property];
        }

        bool perform() override
        {
            showCorrectTab();
            getComponent()->getProperties().set (property, newValue);
            updateFilmstripPreview (layout, getComponent());
            changed();
            return true;
        }

        bool undo() override
        {
            showCorrectTab();
            getComponent()->getProperties().set (property, oldValue);
            updateFilmstripPreview (layout, getComponent());
            changed();
            return true;
        }

        Identifier property;
        var newValue, oldValue;
    };

    static void setFilmstripImage (ComponentLayout& layout, Slider* slider, const String& image, bool undoable)
    {
        if (getFilmstripImage (slider) == image)
            return;

        if (undoable)
            layout.perform (std::make_unique<SetFilmstripPropertyAction> (slider, layout, filmstripImageProperty(), image),
                            "Change slider filmstrip image");
        else
        {
            slider->getProperties().set (filmstripImageProperty(), image);
            updateFilmstripPreview (layout, slider);
            layout.changed();
        }
    }

    static void setFilmstripFrames (ComponentLayout& layout, Slider* slider, int frames, bool undoable)
    {
        frames = jmax (1, frames);

        if (getFilmstripFrames (slider) == frames)
            return;

        if (undoable)
            layout.perform (std::make_unique<SetFilmstripPropertyAction> (slider, layout, filmstripFramesProperty(), frames),
                            "Change slider filmstrip frame count");
        else
        {
            slider->getProperties().set (filmstripFramesProperty(), frames);
            updateFilmstripPreview (layout, slider);
            layout.changed();
        }
    }

    static void setFilmstripVertical (ComponentLayout& layout, Slider* slider, bool vertical, bool undoable)
    {
        if (isFilmstripVertical (slider) == vertical)
            return;

        if (undoable)
            layout.perform (std::make_unique<SetFilmstripPropertyAction> (slider, layout, filmstripVerticalProperty(), vertical),
                            "Change slider filmstrip layout");
        else
        {
            slider->getProperties().set (filmstripVerticalProperty(), vertical);
            updateFilmstripPreview (layout, slider);
            layout.changed();
        }
    }

    static String getImageCreationCode (Slider* slider)
    {
        const auto resourceName = getFilmstripImage (slider);

        if (resourceName.isEmpty())
            return "juce::Image()";

        return "juce::ImageCache::getFromMemory (" + resourceName + ", " + resourceName + "Size)";
    }

    static void addFilmstripCreationCode (GeneratedCode& code, Slider* slider,
                                          const String& memberVariableName, String& constructorCode)
    {
        if (getFilmstripImage (slider).isEmpty())
            return;

        const auto suffix = String (code.getUniqueSuffix());
        const auto lookAndFeelClass = "FilmstripSliderLookAndFeel" + suffix;
        const auto lookAndFeelMember = "filmstripSliderLookAndFeel" + suffix;

        code.privateMemberDeclarations
            << "struct " << lookAndFeelClass << "  : public juce::LookAndFeel_V4\n"
            << "{\n"
            << "    void setFilmstrip (juce::Image imageToUse, int numFramesToUse, bool verticalLayoutToUse)\n"
            << "    {\n"
            << "        image = imageToUse;\n"
            << "        numFrames = juce::jmax (1, numFramesToUse);\n"
            << "        verticalLayout = verticalLayoutToUse;\n"
            << "    }\n\n"
            << "    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,\n"
            << "                           float sliderPos, float rotaryStartAngle,\n"
            << "                           float rotaryEndAngle, juce::Slider& slider) override\n"
            << "    {\n"
            << "        if (image.isValid() && numFrames > 1)\n"
            << "        {\n"
            << "            auto frame = juce::jlimit (0, numFrames - 1,\n"
            << "                                      juce::roundToInt (sliderPos * (float) (numFrames - 1)));\n\n"
            << "            if (verticalLayout)\n"
            << "            {\n"
            << "                auto frameHeight = image.getHeight() / numFrames;\n\n"
            << "                if (frameHeight > 0)\n"
            << "                    g.drawImage (image, x, y, width, height,\n"
            << "                                 0, frame * frameHeight, image.getWidth(), frameHeight);\n"
            << "            }\n"
            << "            else\n"
            << "            {\n"
            << "                auto frameWidth = image.getWidth() / numFrames;\n\n"
            << "                if (frameWidth > 0)\n"
            << "                    g.drawImage (image, x, y, width, height,\n"
            << "                                 frame * frameWidth, 0, frameWidth, image.getHeight());\n"
            << "            }\n\n"
            << "            return;\n"
            << "        }\n\n"
            << "        juce::LookAndFeel_V4::drawRotarySlider (g, x, y, width, height, sliderPos,\n"
            << "                                                rotaryStartAngle, rotaryEndAngle, slider);\n"
            << "    }\n\n"
            << "    juce::Image image;\n"
            << "    int numFrames = 1;\n"
            << "    bool verticalLayout = true;\n"
            << "};\n\n"
            << lookAndFeelClass << " " << lookAndFeelMember << ";\n";

        constructorCode << lookAndFeelMember << ".setFilmstrip ("
                        << getImageCreationCode (slider) << ", " << getFilmstripFrames (slider) << ", "
                        << CodeHelpers::boolLiteral (isFilmstripVertical (slider)) << ");\n";
        constructorCode << memberVariableName << "->setLookAndFeel (&" << lookAndFeelMember << ");\n";
        code.destructorCode << memberVariableName << "->setLookAndFeel (nullptr);\n";
    }

    class FilmstripImageProperty  : public ImageResourceProperty<Slider>
    {
    public:
        FilmstripImageProperty (ComponentLayout& layout_, Slider* owner)
            : ImageResourceProperty<Slider> (*layout_.getDocument(), owner, "filmstrip image", true),
              layout (layout_)
        {
        }

        void setResource (const String& newName) override     { setFilmstripImage (layout, element, newName, true); }
        String getResource() const override                   { return getFilmstripImage (element); }

    private:
        ComponentLayout& layout;
    };

    struct FilmstripFramesProperty  : public ComponentTextProperty<Slider>
    {
        FilmstripFramesProperty (ComponentLayout& layout_, Slider* slider)
            : ComponentTextProperty<Slider> ("filmstrip frames", 8, false, slider, *layout_.getDocument()),
              layout (layout_)
        {
        }

        void setText (const String& newText) override         { setFilmstripFrames (layout, component, newText.getIntValue(), true); }
        String getText() const override                       { return String (getFilmstripFrames (component)); }

        ComponentLayout& layout;
    };

    struct FilmstripLayoutProperty  : public ComponentChoiceProperty<Slider>
    {
        FilmstripLayoutProperty (ComponentLayout& layout_, Slider* slider)
            : ComponentChoiceProperty<Slider> ("filmstrip layout", slider, *layout_.getDocument()),
              layout (layout_)
        {
            choices.add ("Vertical");
            choices.add ("Horizontal");
        }

        void setIndex (int newIndex) override
        {
            setFilmstripVertical (layout, component, newIndex != 1, true);
        }

        int getIndex() const override
        {
            return isFilmstripVertical (component) ? 0 : 1;
        }

        ComponentLayout& layout;
    };

    //==============================================================================
    struct SliderTypeProperty  : public ComponentChoiceProperty<Slider>
    {
        SliderTypeProperty (Slider* slider, JucerDocument& doc)
            : ComponentChoiceProperty<Slider> ("type", slider, doc)
        {
            choices.add ("Linear Horizontal");
            choices.add ("Linear Vertical");
            choices.add ("Linear Bar Horizontal");
            choices.add ("Linear Bar Vertical");
            choices.add ("Rotary");
            choices.add ("Rotary HorizontalDrag");
            choices.add ("Rotary VerticalDrag");
            choices.add ("Rotary HorizontalVerticalDrag");
            choices.add ("Inc/Dec Buttons");
            choices.add ("Two Value Horizontal");
            choices.add ("Two Value Vertical");
            choices.add ("Three Value Horizontal");
            choices.add ("Three Value Vertical");
        }

        void setIndex (int newIndex) override
        {
            if (newIndex >= 0 && newIndex < numElementsInArray (sliderStyleTypes))
                document.perform (new SliderTypeChangeAction (component, *document.getComponentLayout(),
                                                              sliderStyleTypes[newIndex]),
                                  "Change Slider style");
        }

        int getIndex() const override
        {
            for (int i = 0; i < numElementsInArray (sliderStyleTypes); ++i)
                if (sliderStyleTypes[i] == dynamic_cast<Slider*> (component)->getSliderStyle())
                    return i;

            return -1;
        }

    private:
        struct SliderTypeChangeAction  : public ComponentUndoableAction<Slider>
        {
            SliderTypeChangeAction (Slider* comp, ComponentLayout& l, Slider::SliderStyle newState_)
                : ComponentUndoableAction<Slider> (comp, l),
                  newState (newState_)
            {
                oldState = comp->getSliderStyle();
            }

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setSliderStyle (newState);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setSliderStyle (oldState);
                changed();
                return true;
            }

            Slider::SliderStyle newState, oldState;
        };
    };

    //==============================================================================
    struct SliderTextboxProperty  : public ComponentChoiceProperty<Slider>
    {
        SliderTextboxProperty (Slider* slider, JucerDocument& doc)
            : ComponentChoiceProperty<Slider> ("text position", slider, doc)
        {
            choices.add ("No text box");
            choices.add ("Text box on left");
            choices.add ("Text box on right");
            choices.add ("Text box above");
            choices.add ("Text box below");
        }

        void setIndex (int newIndex) override
        {
            if (newIndex >= 0 && newIndex < numElementsInArray (sliderTextBoxPositions))
                document.perform (new SliderTextBoxChangeAction (component, *document.getComponentLayout(),
                                                                 sliderTextBoxPositions[newIndex]),
                                  "Change Slider textbox");
        }

        int getIndex() const override
        {
            for (int i = 0; i < numElementsInArray (sliderTextBoxPositions); ++i)
                if (sliderTextBoxPositions[i] == component->getTextBoxPosition())
                    return i;

            return -1;
        }

    private:
        struct SliderTextBoxChangeAction  : public ComponentUndoableAction<Slider>
        {
            SliderTextBoxChangeAction (Slider* comp, ComponentLayout& l, Slider::TextEntryBoxPosition newState_)
                : ComponentUndoableAction<Slider> (comp, l),
                  newState (newState_)
            {
                oldState = comp->getTextBoxPosition();
            }

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setTextBoxStyle (newState,
                                                 ! getComponent()->isTextBoxEditable(),
                                                 getComponent()->getTextBoxWidth(),
                                                 getComponent()->getTextBoxHeight());
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setTextBoxStyle (oldState,
                                                 ! getComponent()->isTextBoxEditable(),
                                                 getComponent()->getTextBoxWidth(),
                                                 getComponent()->getTextBoxHeight());
                changed();
                return true;
            }

            Slider::TextEntryBoxPosition newState, oldState;
        };
    };

    //==============================================================================
    struct SliderTextboxEditableProperty  : public ComponentBooleanProperty<Slider>
    {
        SliderTextboxEditableProperty (Slider* slider, JucerDocument& doc)
            : ComponentBooleanProperty<Slider> ("text box mode", "Editable", "Editable", slider, doc)
        {
        }

        void setState (bool newState) override
        {
            document.perform (new SliderEditableChangeAction (component, *document.getComponentLayout(), newState),
                              "Change Slider editability");
        }

        bool getState() const override
        {
            return component->isTextBoxEditable();
        }

    private:
        struct SliderEditableChangeAction  : public ComponentUndoableAction<Slider>
        {
            SliderEditableChangeAction (Slider* const comp, ComponentLayout& l, bool newState_)
                : ComponentUndoableAction<Slider> (comp, l),
                  newState (newState_)
            {
                oldState = comp->isTextBoxEditable();
            }

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setTextBoxIsEditable (newState);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setTextBoxIsEditable (oldState);
                changed();
                return true;
            }

            bool newState, oldState;
        };
    };

    //==============================================================================
    struct SliderCallbackProperty  : public ComponentBooleanProperty<Slider>
    {
        SliderCallbackProperty (Slider* s, JucerDocument& doc)
            : ComponentBooleanProperty<Slider> ("callback", "Generate SliderListener",
                                                "Generate SliderListener", s, doc)
        {
        }

        void setState (bool newState) override
        {
            document.perform (new SliderCallbackChangeAction (component, *document.getComponentLayout(), newState),
                              "Change button callback");
        }

        bool getState() const override       { return needsSliderListener (component); }

        struct SliderCallbackChangeAction  : public ComponentUndoableAction<Slider>
        {
            SliderCallbackChangeAction (Slider* comp, ComponentLayout& l, bool newState_)
                : ComponentUndoableAction<Slider> (comp, l),
                  newState (newState_)
            {
                oldState = needsSliderListener (comp);
            }

            bool perform() override
            {
                showCorrectTab();
                setNeedsSliderListener (getComponent(), newState);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                setNeedsSliderListener (getComponent(), oldState);
                changed();
                return true;
            }

            bool newState, oldState;
        };
    };

    //==============================================================================
    struct SliderTextboxSizeProperty  : public ComponentTextProperty<Slider>
    {
        SliderTextboxSizeProperty (Slider* slider, JucerDocument& doc, bool isWidth_)
            : ComponentTextProperty<Slider> (isWidth_ ? "text box width" : "text box height",
                                             12, false, slider, doc),
              isWidth (isWidth_)
        {
        }

        void setText (const String& newText) override
        {
            document.perform (new SliderBoxSizeChangeAction (component, *document.getComponentLayout(), isWidth, newText.getIntValue()),
                              "Change Slider textbox size");
        }

        String getText() const override
        {
            return String (isWidth ? component->getTextBoxWidth()
                                   : component->getTextBoxHeight());
        }

    private:
        const bool isWidth;

        struct SliderBoxSizeChangeAction  : public ComponentUndoableAction<Slider>
        {
            SliderBoxSizeChangeAction (Slider* const comp, ComponentLayout& l, bool isWidth_, int newSize_)
                : ComponentUndoableAction<Slider> (comp, l),
                  isWidth (isWidth_),
                  newSize (newSize_)
            {
                oldSize = isWidth ? comp->getTextBoxWidth()
                                  : comp->getTextBoxHeight();
            }

            bool perform() override
            {
                showCorrectTab();
                Slider& c = *getComponent();

                if (isWidth)
                    c.setTextBoxStyle (c.getTextBoxPosition(),
                                       ! c.isTextBoxEditable(),
                                       newSize,
                                       c.getTextBoxHeight());
                else
                    c.setTextBoxStyle (c.getTextBoxPosition(),
                                       ! c.isTextBoxEditable(),
                                       c.getTextBoxWidth(),
                                       newSize);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                Slider& c = *getComponent();

                if (isWidth)
                    c.setTextBoxStyle (c.getTextBoxPosition(),
                                       ! c.isTextBoxEditable(),
                                       oldSize,
                                       c.getTextBoxHeight());
                else
                    c.setTextBoxStyle (c.getTextBoxPosition(),
                                       ! c.isTextBoxEditable(),
                                       c.getTextBoxWidth(),
                                       oldSize);
                changed();
                return true;
            }

            bool isWidth;
            int newSize, oldSize;
        };
    };

    //==============================================================================
    struct SliderRangeProperty  : public ComponentTextProperty<Slider>
    {
        SliderRangeProperty (Slider* slider, JucerDocument& doc,
                             const String& name, int rangeParam_)
            : ComponentTextProperty<Slider> (name, 15, false, slider, doc),
              rangeParam (rangeParam_)
        {
        }

        void setText (const String& newText) override
        {
            double state [3];
            state [0] = component->getMinimum();
            state [1] = component->getMaximum();
            state [2] = component->getInterval();

            state [rangeParam] = newText.getDoubleValue();

            document.perform (new SliderRangeChangeAction (component, *document.getComponentLayout(), state),
                              "Change Slider range");
        }

        String getText() const override
        {
            if (auto* s = dynamic_cast<Slider*> (component))
            {
                switch (rangeParam)
                {
                    case 0:     return String (s->getMinimum());
                    case 1:     return String (s->getMaximum());
                    case 2:     return String (s->getInterval());
                    default:    jassertfalse; break;
                }
            }

            return {};
        }

    private:
        const int rangeParam;

        struct SliderRangeChangeAction  : public ComponentUndoableAction<Slider>
        {
            SliderRangeChangeAction (Slider* comp, ComponentLayout& l, const double newState_[3])
                : ComponentUndoableAction<Slider> (comp, l)
            {
                newState[0] = newState_ [0];
                newState[1] = newState_ [1];
                newState[2] = newState_ [2];

                oldState[0] = comp->getMinimum();
                oldState[1] = comp->getMaximum();
                oldState[2] = comp->getInterval();
            }

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setRange (newState[0], newState[1], newState[2]);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setRange (oldState[0], oldState[1], oldState[2]);
                changed();
                return true;
            }

            double newState[3], oldState[3];
        };
    };

    //==============================================================================
    struct SliderSkewProperty  : public ComponentTextProperty<Slider>
    {
        SliderSkewProperty (Slider* slider, JucerDocument& doc)
            : ComponentTextProperty<Slider> ("skew factor", 12, false, slider, doc)
        {
        }

        void setText (const String& newText) override
        {
            const double skew = jlimit (0.001, 1000.0, newText.getDoubleValue());

            document.perform (new SliderSkewChangeAction (component, *document.getComponentLayout(), skew),
                              "Change Slider skew");
        }

        String getText() const override
        {
            if (auto* s = dynamic_cast<Slider*> (component))
                return String (s->getSkewFactor());

            return {};
        }

        struct SliderSkewChangeAction  : public ComponentUndoableAction<Slider>
        {
            SliderSkewChangeAction (Slider* comp, ComponentLayout& l, double newValue_)
                : ComponentUndoableAction<Slider> (comp, l)
            {
                newValue = newValue_;
                oldValue = comp->getSkewFactor();
            }

            bool perform() override
            {
                showCorrectTab();
                getComponent()->setSkewFactor (newValue);
                changed();
                return true;
            }

            bool undo() override
            {
                showCorrectTab();
                getComponent()->setSkewFactor (oldValue);
                changed();
                return true;
            }

            double newValue, oldValue;
        };
    };

    //==============================================================================
    static String sliderStyleToString (Slider::SliderStyle style)
    {
        switch (style)
        {
            case Slider::LinearHorizontal:              return "LinearHorizontal";
            case Slider::LinearVertical:                return "LinearVertical";
            case Slider::LinearBar:                     return "LinearBar";
            case Slider::LinearBarVertical:             return "LinearBarVertical";
            case Slider::Rotary:                        return "Rotary";
            case Slider::RotaryHorizontalDrag:          return "RotaryHorizontalDrag";
            case Slider::RotaryVerticalDrag:            return "RotaryVerticalDrag";
            case Slider::RotaryHorizontalVerticalDrag:  return "RotaryHorizontalVerticalDrag";
            case Slider::IncDecButtons:                 return "IncDecButtons";
            case Slider::TwoValueHorizontal:            return "TwoValueHorizontal";
            case Slider::TwoValueVertical:              return "TwoValueVertical";
            case Slider::ThreeValueHorizontal:          return "ThreeValueHorizontal";
            case Slider::ThreeValueVertical:            return "ThreeValueVertical";
            default:                                    jassertfalse; break;
        }

        return {};
    }

    static Slider::SliderStyle sliderStringToStyle (const String& s)
    {
        for (int i = 0; i < numElementsInArray (sliderStyleTypes); ++i)
            if (s == sliderStyleToString (sliderStyleTypes[i]))
                return sliderStyleTypes[i];

        jassertfalse;
        return Slider::LinearHorizontal;
    }

    static String textBoxPosToString (const Slider::TextEntryBoxPosition pos)
    {
        switch (pos)
        {
            case Slider::NoTextBox:     return "NoTextBox";
            case Slider::TextBoxLeft:   return "TextBoxLeft";
            case Slider::TextBoxRight:  return "TextBoxRight";
            case Slider::TextBoxAbove:  return "TextBoxAbove";
            case Slider::TextBoxBelow:  return "TextBoxBelow";
            default:                    jassertfalse; break;
        }

        return {};
    }

    static Slider::TextEntryBoxPosition stringToTextBoxPos (const String& s)
    {
        for (int i = 0; i < numElementsInArray (sliderTextBoxPositions); ++i)
            if (s == textBoxPosToString (sliderTextBoxPositions[i]))
                return sliderTextBoxPositions[i];

        jassertfalse;
        return Slider::TextBoxLeft;
    }
};
