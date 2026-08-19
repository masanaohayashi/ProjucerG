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


#include "jucer_SeparatorComponent.h"


//==============================================================================
class SeparatorComponentHandler  : public ComponentTypeHandler
{
public:
    SeparatorComponentHandler()
        : ComponentTypeHandler ("Separator", "JucerSeparatorBar", typeid (SeparatorComponent), 8, 200)
    {
        registerColour (juce::ResizableWindow::backgroundColourId, "bar", "barcol");
    }

    Component* createNewComponent (JucerDocument*) override
    {
        return new SeparatorComponent();
    }

    XmlElement* createXmlFor (Component* comp, const ComponentLayout* layout) override
    {
        auto* s = dynamic_cast<SeparatorComponent*> (comp);
        auto* e = ComponentTypeHandler::createXmlFor (comp, layout);

        e->setAttribute ("before", String (s->beforeId));
        e->setAttribute ("after", String (s->afterId));
        e->setAttribute ("resizemode", SeparatorComponent::toString (s->resizeMode));
        e->setAttribute ("minbefore", s->minSizeBefore);
        e->setAttribute ("minafter", s->minSizeAfter);

        return e;
    }

    bool restoreFromXml (const XmlElement& xml, Component* comp, const ComponentLayout* layout) override
    {
        auto* s = dynamic_cast<SeparatorComponent*> (comp);

        if (! ComponentTypeHandler::restoreFromXml (xml, comp, layout))
            return false;

        s->beforeId      = xml.getStringAttribute ("before", "0").getLargeIntValue();
        s->afterId       = xml.getStringAttribute ("after", "0").getLargeIntValue();
        s->resizeMode    = SeparatorComponent::fromString (xml.getStringAttribute ("resizemode"));
        s->minSizeBefore = xml.getIntAttribute ("minbefore", 0);
        s->minSizeAfter  = xml.getIntAttribute ("minafter", 0);

        return true;
    }

    //==============================================================================
    void getEditableProperties (Component* component, JucerDocument& document,
                                Array<PropertyComponent*>& props, bool multipleSelected) override
    {
        ComponentTypeHandler::getEditableProperties (component, document, props, multipleSelected);

        if (multipleSelected)
            return;

        if (auto* s = dynamic_cast<SeparatorComponent*> (component))
        {
            props.add (new TargetProperty (s, document, true));
            props.add (new TargetProperty (s, document, false));
            props.add (new ResizeModeProperty (s, document));
            props.add (new MinSizeProperty (s, document, true));
            props.add (new MinSizeProperty (s, document, false));
        }

        addColourProperties (component, document, props);
    }

    //==============================================================================
    String getCreationParameters (GeneratedCode& code, Component* component) override
    {
        auto* s = dynamic_cast<SeparatorComponent*> (component);
        auto* layout = code.document->getComponentLayout();

        captureDesignedSplit (s, layout);

        static const char* const modeCode[] = { "proportional", "keepBeforeFixed", "keepAfterFixed" };

        return String (s->isVertical() ? "true" : "false")
                + ", JucerSeparatorBar::" + modeCode[s->resizeMode]
                + ", " + CodeHelpers::floatLiteral (s->getProportion(), 4)
                + ", " + String (s->getFixedSize())
                + ", " + String (s->minSizeBefore)
                + ", " + String (s->minSizeAfter);
    }

    void fillInMemberVariableDeclarations (GeneratedCode& code, Component* component,
                                           const String& memberVariableName) override
    {
        if (! code.privateMemberDeclarations.contains ("class JucerSeparatorBar"))
            code.privateMemberDeclarations = getSeparatorClassCode() + code.privateMemberDeclarations;

        ComponentTypeHandler::fillInMemberVariableDeclarations (code, component, memberVariableName);
    }

    void fillInCreationCode (GeneratedCode& code, Component* component, const String& memberVariableName) override
    {
        ComponentTypeHandler::fillInCreationCode (code, component, memberVariableName);

        code.constructorCode << getColourIntialisationCode (component, memberVariableName) << '\n';
    }

    void fillInResizeCode (GeneratedCode& code, Component* component, const String& memberVariableName) override
    {
        ComponentTypeHandler::fillInResizeCode (code, component, memberVariableName);

        // (wiring up the targets here rather than in the constructor, because resized() is
        // only ever reached once every member has been created)
        auto* s = dynamic_cast<SeparatorComponent*> (component);
        auto* layout = code.document->getComponentLayout();

        code.getCallbackCode (String(), "void", "resized()", false)
            << memberVariableName << "->setTargets (" << memberName (layout, s->beforeId)
            << ", " << memberName (layout, s->afterId) << ");\n";
    }

private:
    //==============================================================================
    static String memberName (const ComponentLayout* layout, int64 id)
    {
        if (auto* c = id != 0 ? layout->findComponentWithId (id) : nullptr)
            return layout->getComponentMemberVariableName (c) + ".get()";

        return "nullptr";
    }

    static void captureDesignedSplit (SeparatorComponent* s, const ComponentLayout* layout)
    {
        Array<Component*> siblings;

        for (int i = 0; i < layout->getNumComponents(); ++i)
            siblings.add (layout->getComponent (i));

        s->findTargetsIn (siblings);
        s->captureDesignedSplit();
    }

    static String getSeparatorClassCode()
    {
        return R"(//==============================================================================
/** A draggable splitter bar, generated by the Projucer's GUI editor.

    It splits the area covered by the two components it was given in the editor,
    keeping their position on the other axis untouched. The layout is re-applied
    from parentSizeChanged(), i.e. straight after resized() has run.
*/
class JucerSeparatorBar  : public juce::Component
{
public:
    /** What happens to the two halves when the parent component is resized. */
    enum ResizeMode
    {
        proportional = 0,   /**< both halves keep their share of the area. */
        keepBeforeFixed,    /**< the "before" half keeps its size, "after" absorbs the change. */
        keepAfterFixed      /**< the "after" half keeps its size, "before" absorbs the change. */
    };

    JucerSeparatorBar (bool isVerticalBar, ResizeMode mode, double initialProportion,
                       int initialFixedSize, int minBefore, int minAfter)
        : vertical (isVerticalBar), resizeMode (mode), proportion (initialProportion),
          fixedSize (initialFixedSize), minSizeBefore (minBefore), minSizeAfter (minAfter)
    {
        setMouseCursor (vertical ? juce::MouseCursor::LeftRightResizeCursor
                                 : juce::MouseCursor::UpDownResizeCursor);
    }

    void setTargets (juce::Component* componentBefore, juce::Component* componentAfter)
    {
        before = componentBefore;
        after = componentAfter;
    }

    double getProportion() const noexcept       { return proportion; }
    int getFixedSize() const noexcept           { return fixedSize; }

    /** Moves the bar, as a fraction of the space it can travel through. */
    void setProportion (double newProportion)
    {
        setSplitPosition (juce::roundToInt (getTravel() * newProportion), getTravel());
        applyLayout();
    }

    void paint (juce::Graphics& g) override
    {
        auto background = findColour (juce::ResizableWindow::backgroundColourId);

        g.fillAll (isColourSpecified (juce::ResizableWindow::backgroundColourId)
                       ? background
                       : background.contrasting (isMouseOverOrDragging() ? 0.2f : 0.1f));
    }

    void mouseEnter (const juce::MouseEvent&) override      { repaint(); }
    void mouseExit  (const juce::MouseEvent&) override      { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStart = positionInParent (e);
        splitAtDragStart = getWantedSplitPosition (getTravel());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto travel = getTravel();

        if (travel <= 0)
            return;

        setSplitPosition (juce::jlimit (0, travel, splitAtDragStart + positionInParent (e) - dragStart), travel);
        applyLayout();
    }

    void parentSizeChanged() override                       { applyLayout(); }

private:
    int positionInParent (const juce::MouseEvent& e) const
    {
        auto* parent = getParentComponent();

        if (parent == nullptr)
            return vertical ? e.x : e.y;

        auto pos = e.getEventRelativeTo (parent).getPosition();
        return vertical ? pos.x : pos.y;
    }

    juce::Rectangle<int> getSplitArea() const
    {
        auto area = getBounds();

        if (before != nullptr)  area = area.getUnion (before->getBounds());
        if (after != nullptr)   area = area.getUnion (after->getBounds());

        return area;
    }

    int getTravel() const
    {
        auto area = getSplitArea();

        return (vertical ? area.getWidth() : area.getHeight())
                 - (vertical ? getWidth() : getHeight());
    }

    int getWantedSplitPosition (int travel) const
    {
        if (resizeMode == keepBeforeFixed)  return fixedSize;
        if (resizeMode == keepAfterFixed)   return travel - fixedSize;

        return juce::roundToInt (travel * proportion);
    }

    void setSplitPosition (int pos, int travel)
    {
        proportion = travel > 0 ? (double) pos / (double) travel : 0.5;
        fixedSize = resizeMode == keepAfterFixed ? travel - pos : pos;
    }

    void applyLayout()
    {
        if (before == nullptr || after == nullptr)
            return;

        const auto area = getSplitArea();
        const auto thickness = vertical ? getWidth() : getHeight();
        const auto travel = (vertical ? area.getWidth() : area.getHeight()) - thickness;

        if (travel <= 0)
            return;

        const auto minA = juce::jlimit (0, travel, minSizeBefore);
        const auto minB = juce::jlimit (0, travel - minA, minSizeAfter);

        const auto pos = juce::jlimit (minA, travel - minB, getWantedSplitPosition (travel));
        setSplitPosition (pos, travel);

        if (vertical)
        {
            before->setBounds (area.getX(), before->getY(), pos, before->getHeight());
            setBounds (area.getX() + pos, getY(), thickness, getHeight());
            after->setBounds (area.getX() + pos + thickness, after->getY(),
                              area.getWidth() - pos - thickness, after->getHeight());
        }
        else
        {
            before->setBounds (before->getX(), area.getY(), before->getWidth(), pos);
            setBounds (getX(), area.getY() + pos, getWidth(), thickness);
            after->setBounds (after->getX(), area.getY() + pos + thickness,
                              after->getWidth(), area.getHeight() - pos - thickness);
        }
    }

    const bool vertical;
    const ResizeMode resizeMode;
    double proportion;
    int fixedSize, splitAtDragStart = 0, dragStart = 0;
    const int minSizeBefore, minSizeAfter;
    juce::Component::SafePointer<juce::Component> before, after;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JucerSeparatorBar)
};

)";
    }

    //==============================================================================
    class SetTargetAction  : public ComponentUndoableAction <SeparatorComponent>
    {
    public:
        SetTargetAction (SeparatorComponent* const comp, ComponentLayout& l, bool isBefore, int64 newId)
            : ComponentUndoableAction <SeparatorComponent> (comp, l),
              before (isBefore), newValue (newId),
              oldValue (isBefore ? comp->beforeId : comp->afterId)
        {}

        bool perform() override     { return apply (newValue); }
        bool undo() override        { return apply (oldValue); }

    private:
        bool apply (int64 v)
        {
            showCorrectTab();
            (before ? getComponent()->beforeId : getComponent()->afterId) = v;
            changed();
            return true;
        }

        const bool before;
        const int64 newValue, oldValue;
    };

    //==============================================================================
    class TargetProperty  : public ComponentChoiceProperty <SeparatorComponent>
    {
    public:
        TargetProperty (SeparatorComponent* comp, JucerDocument& doc, bool isBefore)
            : ComponentChoiceProperty <SeparatorComponent> (isBefore ? "component before" : "component after", comp, doc),
              before (isBefore)
        {
            choices.add ("(none)");
            ids.add (0);

            if (auto* layout = doc.getComponentLayout())
            {
                for (int i = 0; i < layout->getNumComponents(); ++i)
                {
                    auto* c = layout->getComponent (i);

                    if (c != nullptr && c != comp)
                    {
                        choices.add (layout->getComponentMemberVariableName (c));
                        ids.add (ComponentTypeHandler::getComponentId (c));
                    }
                }
            }
        }

        void setIndex (int newIndex) override
        {
            if (isPositiveAndBelow (newIndex, ids.size()))
                document.perform (new SetTargetAction (component, *document.getComponentLayout(), before, ids[newIndex]),
                                  "Change separator target");
        }

        int getIndex() const override
        {
            return jmax (0, ids.indexOf (before ? component->beforeId : component->afterId));
        }

    private:
        const bool before;
        Array<int64> ids;
    };

    //==============================================================================
    class SetResizeModeAction  : public ComponentUndoableAction <SeparatorComponent>
    {
    public:
        SetResizeModeAction (SeparatorComponent* const comp, ComponentLayout& l, SeparatorComponent::ResizeMode m)
            : ComponentUndoableAction <SeparatorComponent> (comp, l),
              newValue (m), oldValue (comp->resizeMode)
        {}

        bool perform() override     { return apply (newValue); }
        bool undo() override        { return apply (oldValue); }

    private:
        bool apply (SeparatorComponent::ResizeMode m)
        {
            showCorrectTab();
            getComponent()->resizeMode = m;
            changed();
            return true;
        }

        const SeparatorComponent::ResizeMode newValue, oldValue;
    };

    //==============================================================================
    class ResizeModeProperty  : public ComponentChoiceProperty <SeparatorComponent>
    {
    public:
        ResizeModeProperty (SeparatorComponent* comp, JucerDocument& doc)
            : ComponentChoiceProperty <SeparatorComponent> ("on parent resize", comp, doc)
        {
            choices = SeparatorComponent::getResizeModeNames();
        }

        void setIndex (int newIndex) override
        {
            if (isPositiveAndBelow (newIndex, choices.size()))
                document.perform (new SetResizeModeAction (component, *document.getComponentLayout(),
                                                           (SeparatorComponent::ResizeMode) newIndex),
                                  "Change separator resize mode");
        }

        int getIndex() const override       { return (int) component->resizeMode; }
    };

    //==============================================================================
    class SetMinSizeAction  : public ComponentUndoableAction <SeparatorComponent>
    {
    public:
        SetMinSizeAction (SeparatorComponent* const comp, ComponentLayout& l, bool isBefore, int newSize)
            : ComponentUndoableAction <SeparatorComponent> (comp, l),
              before (isBefore), newValue (newSize),
              oldValue (isBefore ? comp->minSizeBefore : comp->minSizeAfter)
        {}

        bool perform() override     { return apply (newValue); }
        bool undo() override        { return apply (oldValue); }

    private:
        bool apply (int v)
        {
            showCorrectTab();
            (before ? getComponent()->minSizeBefore : getComponent()->minSizeAfter) = v;
            changed();
            return true;
        }

        const bool before;
        const int newValue, oldValue;
    };

    //==============================================================================
    class MinSizeProperty  : public ComponentTextProperty <SeparatorComponent>
    {
    public:
        MinSizeProperty (SeparatorComponent* comp, JucerDocument& doc, bool isBefore)
            : ComponentTextProperty <SeparatorComponent> (isBefore ? "min size before" : "min size after",
                                                          8, false, comp, doc),
              before (isBefore)
        {}

        void setText (const String& newText) override
        {
            document.perform (new SetMinSizeAction (component, *document.getComponentLayout(),
                                                    before, jmax (0, newText.getIntValue())),
                              "Change separator minimum size");
        }

        String getText() const override
        {
            return String (before ? component->minSizeBefore : component->minSizeAfter);
        }

    private:
        const bool before;
    };
};
