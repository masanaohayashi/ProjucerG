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
class DrawableButtonHandler  : public ButtonHandler
{
public:
    enum DrawableRole
    {
        normalImage = 0,
        overImage = 1,
        downImage = 2,
        disabledImage = 3
    };

    DrawableButtonHandler()
        : ButtonHandler ("Drawable Button", "juce::DrawableButton", typeid (DrawableButton), 150, 32)
    {
        registerColour (juce::DrawableButton::textColourId, "text", "textCol");
        registerColour (juce::DrawableButton::textColourOnId, "text on", "textColOn");
        registerColour (juce::DrawableButton::backgroundColourId, "background", "bgCol");
        registerColour (juce::DrawableButton::backgroundOnColourId, "background on", "bgColOn");
        registerColour (juce::TextButton::buttonColourId, "button background", "buttonBgCol");
        registerColour (juce::TextButton::buttonOnColourId, "button background on", "buttonBgColOn");
    }

    Component* createNewComponent (JucerDocument*) override
    {
        return new DrawableButton ("new button", DrawableButton::ImageFitted);
    }

    String getCreationParameters (GeneratedCode& code, Component* component) override
    {
        return quotedString (component->getName(), code.shouldUseTransMacro()) + ", "
             + getStyleCode (getDrawableButtonStyle (dynamic_cast<DrawableButton*> (component)));
    }

    void getEditableProperties (Component* component, JucerDocument& document,
                                Array<PropertyComponent*>& props, bool multipleSelected) override
    {
        ButtonHandler::getEditableProperties (component, document, props, multipleSelected);

        if (multipleSelected)
            return;

        addColourProperties (component, document, props);

        if (auto* button = dynamic_cast<DrawableButton*> (component))
        {
            auto& layout = *document.getComponentLayout();

            props.add (new DrawableButtonStyleProperty (layout, button));
            props.add (new DrawableButtonEdgeIndentProperty (layout, button));
            props.add (new DrawableButtonResourceProperty (layout, button, normalImage, "normal drawable"));
            props.add (new DrawableButtonResourceProperty (layout, button, overImage, "over drawable"));
            props.add (new DrawableButtonResourceProperty (layout, button, downImage, "down drawable"));
            props.add (new DrawableButtonResourceProperty (layout, button, disabledImage, "disabled drawable"));
        }
    }

    XmlElement* createXmlFor (Component* comp, const ComponentLayout* layout) override
    {
        auto* e = ButtonHandler::createXmlFor (comp, layout);
        auto* button = dynamic_cast<DrawableButton*> (comp);

        e->setAttribute ("buttonStyle", styleToString (getDrawableButtonStyle (button)));
        e->setAttribute ("edgeIndent", getEdgeIndent (button));
        e->setAttribute ("resourceNormal", getDrawableResource (button, normalImage));
        e->setAttribute ("resourceOver", getDrawableResource (button, overImage));
        e->setAttribute ("resourceDown", getDrawableResource (button, downImage));
        e->setAttribute ("resourceDisabled", getDrawableResource (button, disabledImage));

        return e;
    }

    bool restoreFromXml (const XmlElement& xml, Component* comp, const ComponentLayout* layout) override
    {
        if (! ButtonHandler::restoreFromXml (xml, comp, layout))
            return false;

        auto* button = dynamic_cast<DrawableButton*> (comp);
        auto& l = const_cast<ComponentLayout&> (*layout);

        setDrawableButtonStyle (l, button, stringToStyle (xml.getStringAttribute ("buttonStyle", "ImageFitted")), false);
        setEdgeIndent (l, button, xml.getIntAttribute ("edgeIndent", 3), false);
        setDrawableResource (l, button, normalImage, xml.getStringAttribute ("resourceNormal", String()), false);
        setDrawableResource (l, button, overImage, xml.getStringAttribute ("resourceOver", String()), false);
        setDrawableResource (l, button, downImage, xml.getStringAttribute ("resourceDown", String()), false);
        setDrawableResource (l, button, disabledImage, xml.getStringAttribute ("resourceDisabled", String()), false);

        return true;
    }

    void fillInCreationCode (GeneratedCode& code, Component* component, const String& memberVariableName) override
    {
        ButtonHandler::fillInCreationCode (code, component, memberVariableName);

        auto* button = dynamic_cast<DrawableButton*> (component);
        String s;

        s << getColourIntialisationCode (component, memberVariableName);

        if (getEdgeIndent (button) != 3)
            s << memberVariableName << "->setEdgeIndent (" << getEdgeIndent (button) << ");\n";

        addDrawableCreationCode (code, s, button, memberVariableName);
        s << '\n';
        code.constructorCode += s;
    }

private:
    static Identifier resourceProperty (DrawableRole role)     { return "drawableResource" + String ((int) role); }
    static Identifier styleProperty()                          { return "drawableButtonStyle"; }
    static Identifier edgeIndentProperty()                     { return "drawableButtonEdgeIndent"; }

    static DrawableButton::ButtonStyle getDrawableButtonStyle (DrawableButton* button)
    {
        return (DrawableButton::ButtonStyle) (int) button->getProperties().getWithDefault (styleProperty(), (int) DrawableButton::ImageFitted);
    }

    static int getEdgeIndent (DrawableButton* button)
    {
        return (int) button->getProperties().getWithDefault (edgeIndentProperty(), button->getEdgeIndent());
    }

    static String getDrawableResource (DrawableButton* button, DrawableRole role)
    {
        return button->getProperties()[resourceProperty (role)].toString();
    }

    static std::unique_ptr<Drawable> createDrawableForPreview (JucerDocument& document, const String& resourceName)
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
                        return Drawable::createFromImageFile (file);
                }
            }

            return {};
        }

        if (auto* drawable = document.getResources().getDrawable (resourceName))
            return drawable->createCopy();

        return {};
    }

    static void updateButtonImages (ComponentLayout& layout, DrawableButton* button)
    {
        auto normal = createDrawableForPreview (*layout.getDocument(), getDrawableResource (button, normalImage));
        auto over = createDrawableForPreview (*layout.getDocument(), getDrawableResource (button, overImage));
        auto down = createDrawableForPreview (*layout.getDocument(), getDrawableResource (button, downImage));
        auto disabled = createDrawableForPreview (*layout.getDocument(), getDrawableResource (button, disabledImage));

        if (normal == nullptr)
        {
            if (over != nullptr)           normal = over->createCopy();
            else if (down != nullptr)      normal = down->createCopy();
            else if (disabled != nullptr)  normal = disabled->createCopy();
        }

        if (normal != nullptr)
            button->setImages (normal.get(), over.get(), down.get(), disabled.get());

        button->setButtonStyle (getDrawableButtonStyle (button));
        button->setEdgeIndent (getEdgeIndent (button));
        button->repaint();
    }

    struct SetDrawableButtonPropertyAction  : public ComponentUndoableAction<DrawableButton>
    {
        SetDrawableButtonPropertyAction (DrawableButton* button, ComponentLayout& layout_, Identifier property_, var newValue_)
            : ComponentUndoableAction<DrawableButton> (button, layout_),
              property (property_),
              newValue (std::move (newValue_))
        {
            oldValue = button->getProperties()[property];
        }

        bool perform() override
        {
            showCorrectTab();
            getComponent()->getProperties().set (property, newValue);
            updateButtonImages (layout, getComponent());
            changed();
            return true;
        }

        bool undo() override
        {
            showCorrectTab();
            getComponent()->getProperties().set (property, oldValue);
            updateButtonImages (layout, getComponent());
            changed();
            return true;
        }

        Identifier property;
        var newValue, oldValue;
    };

    static void setDrawableResource (ComponentLayout& layout, DrawableButton* button, DrawableRole role, const String& resourceName, bool undoable)
    {
        auto property = resourceProperty (role);

        if (button->getProperties()[property].toString() == resourceName)
            return;

        if (undoable)
            layout.perform (std::make_unique<SetDrawableButtonPropertyAction> (button, layout, property, resourceName),
                            "Change drawable button resource");
        else
        {
            button->getProperties().set (property, resourceName);
            updateButtonImages (layout, button);
            layout.changed();
        }
    }

    static void setDrawableButtonStyle (ComponentLayout& layout, DrawableButton* button, DrawableButton::ButtonStyle style, bool undoable)
    {
        if (getDrawableButtonStyle (button) == style)
            return;

        if (undoable)
            layout.perform (std::make_unique<SetDrawableButtonPropertyAction> (button, layout, styleProperty(), (int) style),
                            "Change drawable button style");
        else
        {
            button->getProperties().set (styleProperty(), (int) style);
            updateButtonImages (layout, button);
            layout.changed();
        }
    }

    static void setEdgeIndent (ComponentLayout& layout, DrawableButton* button, int edgeIndent, bool undoable)
    {
        edgeIndent = jmax (0, edgeIndent);

        if (getEdgeIndent (button) == edgeIndent)
            return;

        if (undoable)
            layout.perform (std::make_unique<SetDrawableButtonPropertyAction> (button, layout, edgeIndentProperty(), edgeIndent),
                            "Change drawable button edge indent");
        else
        {
            button->getProperties().set (edgeIndentProperty(), edgeIndent);
            updateButtonImages (layout, button);
            layout.changed();
        }
    }

    static String styleToString (DrawableButton::ButtonStyle style)
    {
        switch (style)
        {
            case DrawableButton::ImageFitted:                          return "ImageFitted";
            case DrawableButton::ImageRaw:                             return "ImageRaw";
            case DrawableButton::ImageAboveTextLabel:                  return "ImageAboveTextLabel";
            case DrawableButton::ImageOnButtonBackground:              return "ImageOnButtonBackground";
            case DrawableButton::ImageOnButtonBackgroundOriginalSize:  return "ImageOnButtonBackgroundOriginalSize";
            case DrawableButton::ImageStretched:                       return "ImageStretched";
            default:                                                   jassertfalse; break;
        }

        return "ImageFitted";
    }

    static DrawableButton::ButtonStyle stringToStyle (const String& value)
    {
        for (auto style : { DrawableButton::ImageFitted, DrawableButton::ImageRaw, DrawableButton::ImageAboveTextLabel,
                           DrawableButton::ImageOnButtonBackground, DrawableButton::ImageOnButtonBackgroundOriginalSize,
                           DrawableButton::ImageStretched })
            if (value == styleToString (style))
                return style;

        return DrawableButton::ImageFitted;
    }

    static String getStyleCode (DrawableButton::ButtonStyle style)
    {
        return "juce::DrawableButton::" + styleToString (style);
    }

    static String getDrawableCreationCode (const String& resourceName)
    {
        if (resourceName.isEmpty())
            return "nullptr";

        return "juce::Drawable::createFromImageData (" + resourceName + ", " + resourceName + "Size)";
    }

    static void addDrawableCreationCode (GeneratedCode& code, String& constructorCode, DrawableButton* button, const String& memberVariableName)
    {
        StringArray resources;
        resources.add (getDrawableResource (button, normalImage));
        resources.add (getDrawableResource (button, overImage));
        resources.add (getDrawableResource (button, downImage));
        resources.add (getDrawableResource (button, disabledImage));

        if (resources[0].isEmpty())
            for (int i = 1; i < resources.size(); ++i)
                if (resources[i].isNotEmpty())
                {
                    resources.set (0, resources[i]);
                    break;
                }

        if (resources[0].isEmpty())
            return;

        const auto suffix = String (code.getUniqueSuffix());
        StringArray variableNames;

        for (int i = 0; i < resources.size(); ++i)
        {
            auto variableName = "drawableButtonImage" + suffix + "_" + String (i);
            variableNames.add (variableName);

            if (resources[i].isNotEmpty())
                constructorCode << "auto " << variableName << " = " << getDrawableCreationCode (resources[i]) << ";\n";
        }

        constructorCode << memberVariableName << "->setImages ("
                        << variableNames[0] << ".get(), "
                        << (resources[1].isNotEmpty() ? variableNames[1] + ".get()" : "nullptr") << ", "
                        << (resources[2].isNotEmpty() ? variableNames[2] + ".get()" : "nullptr") << ", "
                        << (resources[3].isNotEmpty() ? variableNames[3] + ".get()" : "nullptr") << ");\n";
    }

    class DrawableButtonResourceProperty  : public ImageResourceProperty<DrawableButton>
    {
    public:
        DrawableButtonResourceProperty (ComponentLayout& layout_, DrawableButton* owner, DrawableRole role_, const String& name)
            : ImageResourceProperty<DrawableButton> (*layout_.getDocument(), owner, name, true),
              layout (layout_),
              role (role_)
        {
        }

        void setResource (const String& newName) override     { setDrawableResource (layout, element, role, newName, true); }
        String getResource() const override                   { return getDrawableResource (element, role); }

        ComponentLayout& layout;
        DrawableRole role;
    };

    struct DrawableButtonStyleProperty  : public ComponentChoiceProperty<DrawableButton>
    {
        DrawableButtonStyleProperty (ComponentLayout& layout_, DrawableButton* owner)
            : ComponentChoiceProperty<DrawableButton> ("style", owner, *layout_.getDocument()),
              layout (layout_)
        {
            choices.add ("Image Fitted");
            choices.add ("Image Raw");
            choices.add ("Image Above Text Label");
            choices.add ("Image On Button Background");
            choices.add ("Image On Button Background Original Size");
            choices.add ("Image Stretched");
        }

        void setIndex (int newIndex) override
        {
            setDrawableButtonStyle (layout, component, (DrawableButton::ButtonStyle) jlimit (0, 5, newIndex), true);
        }

        int getIndex() const override
        {
            return (int) getDrawableButtonStyle (component);
        }

        ComponentLayout& layout;
    };

    struct DrawableButtonEdgeIndentProperty  : public ComponentTextProperty<DrawableButton>
    {
        DrawableButtonEdgeIndentProperty (ComponentLayout& layout_, DrawableButton* owner)
            : ComponentTextProperty<DrawableButton> ("edge indent", 8, false, owner, *layout_.getDocument()),
              layout (layout_)
        {
        }

        void setText (const String& newText) override     { setEdgeIndent (layout, component, newText.getIntValue(), true); }
        String getText() const override                   { return String (getEdgeIndent (component)); }

        ComponentLayout& layout;
    };
};
