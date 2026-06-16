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
class ImageComponentHandler  : public ComponentTypeHandler
{
public:
    ImageComponentHandler()
        : ComponentTypeHandler ("Image Component", "juce::ImageComponent", typeid (ImageComponent), 150, 150)
    {
    }

    Component* createNewComponent (JucerDocument*) override
    {
        return new ImageComponent ("new image");
    }

    XmlElement* createXmlFor (Component* comp, const ComponentLayout* layout) override
    {
        auto* e = ComponentTypeHandler::createXmlFor (comp, layout);
        auto* imageComponent = dynamic_cast<ImageComponent*> (comp);

        e->setAttribute ("resource", getImageResource (imageComponent));
        e->setAttribute ("placement", placementToString (getImagePlacement (imageComponent)));

        return e;
    }

    bool restoreFromXml (const XmlElement& xml, Component* comp, const ComponentLayout* layout) override
    {
        if (! ComponentTypeHandler::restoreFromXml (xml, comp, layout))
            return false;

        auto* imageComponent = dynamic_cast<ImageComponent*> (comp);
        auto& l = const_cast<ComponentLayout&> (*layout);

        setImageResource (l, imageComponent, xml.getStringAttribute ("resource", String()), false);
        setImagePlacement (l, imageComponent, stringToPlacement (xml.getStringAttribute ("placement", "centred")), false);

        return true;
    }

    String getCreationParameters (GeneratedCode&, Component* component) override
    {
        return quotedString (component->getName(), false);
    }

    void fillInCreationCode (GeneratedCode& code, Component* component, const String& memberVariableName) override
    {
        ComponentTypeHandler::fillInCreationCode (code, component, memberVariableName);

        auto* imageComponent = dynamic_cast<ImageComponent*> (component);
        auto resource = getImageResource (imageComponent);

        if (resource.isNotEmpty())
        {
            String s;
            s << memberVariableName << "->setImage (juce::ImageCache::getFromMemory ("
              << resource << ", " << resource << "Size), "
              << placementToCode (getImagePlacement (imageComponent)) << ");\n\n";

            code.constructorCode += s;
        }
    }

    void getEditableProperties (Component* component, JucerDocument& document,
                                Array<PropertyComponent*>& props, bool multipleSelected) override
    {
        ComponentTypeHandler::getEditableProperties (component, document, props, multipleSelected);

        if (multipleSelected)
            return;

        if (auto* imageComponent = dynamic_cast<ImageComponent*> (component))
        {
            auto& layout = *document.getComponentLayout();
            props.add (new ImageComponentResourceProperty (layout, imageComponent));
            props.add (new ImageComponentPlacementProperty (layout, imageComponent));
        }
    }

private:
    static Identifier imageResourceProperty()     { return "imageResource"; }
    static Identifier imagePlacementProperty()    { return "imagePlacement"; }

    enum Placement
    {
        stretchToFit = 0,
        centred = 1,
        centredOnlyReduceInSize = 2
    };

    static String getImageResource (ImageComponent* imageComponent)
    {
        return imageComponent->getProperties()[imageResourceProperty()].toString();
    }

    static Placement getImagePlacement (ImageComponent* imageComponent)
    {
        return (Placement) (int) imageComponent->getProperties().getWithDefault (imagePlacementProperty(), (int) centred);
    }

    static RectanglePlacement toRectanglePlacement (Placement placement)
    {
        switch (placement)
        {
            case stretchToFit:               return RectanglePlacement::stretchToFit;
            case centredOnlyReduceInSize:    return RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize;
            case centred:                    return RectanglePlacement::centred;
            default:                         jassertfalse; break;
        }

        return RectanglePlacement::centred;
    }

    static String placementToString (Placement placement)
    {
        switch (placement)
        {
            case stretchToFit:               return "stretchToFit";
            case centredOnlyReduceInSize:    return "centredOnlyReduceInSize";
            case centred:                    return "centred";
            default:                         jassertfalse; break;
        }

        return "centred";
    }

    static Placement stringToPlacement (const String& value)
    {
        if (value == "stretchToFit")               return stretchToFit;
        if (value == "centredOnlyReduceInSize")    return centredOnlyReduceInSize;

        return centred;
    }

    static String placementToCode (Placement placement)
    {
        switch (placement)
        {
            case stretchToFit:               return "juce::RectanglePlacement::stretchToFit";
            case centredOnlyReduceInSize:    return "juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize";
            case centred:                    return "juce::RectanglePlacement::centred";
            default:                         jassertfalse; break;
        }

        return "juce::RectanglePlacement::centred";
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

    static void updateImageComponent (ComponentLayout& layout, ImageComponent* imageComponent)
    {
        imageComponent->setImage (getImageForPreview (*layout.getDocument(), getImageResource (imageComponent)),
                                  toRectanglePlacement (getImagePlacement (imageComponent)));
    }

    struct SetImageComponentPropertyAction  : public ComponentUndoableAction<ImageComponent>
    {
        SetImageComponentPropertyAction (ImageComponent* imageComponent,
                                         ComponentLayout& layout_,
                                         Identifier property_,
                                         var newValue_)
            : ComponentUndoableAction<ImageComponent> (imageComponent, layout_),
              property (property_),
              newValue (std::move (newValue_))
        {
            oldValue = imageComponent->getProperties()[property];
        }

        bool perform() override
        {
            showCorrectTab();
            getComponent()->getProperties().set (property, newValue);
            updateImageComponent (layout, getComponent());
            changed();
            return true;
        }

        bool undo() override
        {
            showCorrectTab();
            getComponent()->getProperties().set (property, oldValue);
            updateImageComponent (layout, getComponent());
            changed();
            return true;
        }

        Identifier property;
        var newValue, oldValue;
    };

    static void setImageResource (ComponentLayout& layout, ImageComponent* imageComponent, const String& resourceName, bool undoable)
    {
        if (getImageResource (imageComponent) == resourceName)
            return;

        if (undoable)
            layout.perform (std::make_unique<SetImageComponentPropertyAction> (imageComponent, layout, imageResourceProperty(), resourceName),
                            "Change image component resource");
        else
        {
            imageComponent->getProperties().set (imageResourceProperty(), resourceName);
            updateImageComponent (layout, imageComponent);
            layout.changed();
        }
    }

    static void setImagePlacement (ComponentLayout& layout, ImageComponent* imageComponent, Placement placement, bool undoable)
    {
        if (getImagePlacement (imageComponent) == placement)
            return;

        if (undoable)
            layout.perform (std::make_unique<SetImageComponentPropertyAction> (imageComponent, layout, imagePlacementProperty(), (int) placement),
                            "Change image component placement");
        else
        {
            imageComponent->getProperties().set (imagePlacementProperty(), (int) placement);
            updateImageComponent (layout, imageComponent);
            layout.changed();
        }
    }

    class ImageComponentResourceProperty  : public ImageResourceProperty<ImageComponent>
    {
    public:
        ImageComponentResourceProperty (ComponentLayout& layout_, ImageComponent* owner)
            : ImageResourceProperty<ImageComponent> (*layout_.getDocument(), owner, "image source", true),
              layout (layout_)
        {
        }

        void setResource (const String& newName) override     { setImageResource (layout, element, newName, true); }
        String getResource() const override                   { return getImageResource (element); }

    private:
        ComponentLayout& layout;
    };

    struct ImageComponentPlacementProperty  : public ComponentChoiceProperty<ImageComponent>
    {
        ImageComponentPlacementProperty (ComponentLayout& layout_, ImageComponent* owner)
            : ComponentChoiceProperty<ImageComponent> ("placement", owner, *layout_.getDocument()),
              layout (layout_)
        {
            choices.add ("Stretch to Fit");
            choices.add ("Maintain aspect ratio");
            choices.add ("Maintain aspect ratio, only reduce in size");
        }

        void setIndex (int newIndex) override
        {
            setImagePlacement (layout, component, (Placement) jlimit (0, 2, newIndex), true);
        }

        int getIndex() const override
        {
            return (int) getImagePlacement (component);
        }

        ComponentLayout& layout;
    };
};
