/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   See LICENSE.md for the terms that apply to this repository.

  ==============================================================================
*/

#include "../Application/jucer_Headers.h"
#include "jucer_GuiDocumentAdapter.h"
#include "../ComponentEditor/jucer_JucerDocument.h"
#include "../ComponentEditor/jucer_ObjectTypes.h"

namespace ProjucerAutomation
{
namespace
{
String sliderStyleToString (Slider::SliderStyle style)
{
    switch (style)
    {
        case Slider::LinearHorizontal:            return "LinearHorizontal";
        case Slider::LinearVertical:              return "LinearVertical";
        case Slider::LinearBar:                   return "LinearBar";
        case Slider::LinearBarVertical:           return "LinearBarVertical";
        case Slider::Rotary:                      return "Rotary";
        case Slider::RotaryHorizontalDrag:        return "RotaryHorizontalDrag";
        case Slider::RotaryVerticalDrag:          return "RotaryVerticalDrag";
        case Slider::RotaryHorizontalVerticalDrag: return "RotaryHorizontalVerticalDrag";
        case Slider::IncDecButtons:               return "IncDecButtons";
        case Slider::TwoValueHorizontal:          return "TwoValueHorizontal";
        case Slider::TwoValueVertical:            return "TwoValueVertical";
        case Slider::ThreeValueHorizontal:        return "ThreeValueHorizontal";
        case Slider::ThreeValueVertical:          return "ThreeValueVertical";
    }

    return "Unknown";
}

String textBoxPositionToString (Slider::TextEntryBoxPosition position)
{
    switch (position)
    {
        case Slider::NoTextBox:    return "NoTextBox";
        case Slider::TextBoxLeft:  return "TextBoxLeft";
        case Slider::TextBoxRight: return "TextBoxRight";
        case Slider::TextBoxAbove: return "TextBoxAbove";
        case Slider::TextBoxBelow: return "TextBoxBelow";
    }

    return "Unknown";
}
}

GuiDocumentAdapter::GuiDocumentAdapter (JucerDocument& documentToUse)
    : document (documentToUse)
{
}

GuiDocumentSnapshot GuiDocumentAdapter::createSnapshot() const
{
    GuiDocumentSnapshot snapshot;
    snapshot.guiFile = document.getCppFile();
    snapshot.componentBounds = { 0, 0, document.getInitialWidth(), document.getInitialHeight() };
    snapshot.snapGridSize = document.getSnappingGridSize();
    snapshot.snapActive = document.isSnapActive (false);
    snapshot.snapShown = document.isSnapShown();

    if (auto* project = document.getCppDocument().getProject())
        snapshot.projectFile = project->getFile();

    if (auto* layout = document.getComponentLayout())
    {
        snapshot.components.reserve ((size_t) layout->getNumComponents());

        for (int i = 0; i < layout->getNumComponents(); ++i)
        {
            auto* component = layout->getComponent (i);
            auto* handler = component != nullptr ? ComponentTypeHandler::getHandlerFor (*component) : nullptr;

            if (component == nullptr || handler == nullptr)
                continue;

            ComponentSnapshot componentSnapshot;
            componentSnapshot.id = ComponentTypeHandler::getComponentId (component);
            componentSnapshot.type = handler->getClassName (component);
            componentSnapshot.name = component->getName();
            componentSnapshot.memberName = layout->getComponentMemberVariableName (component);
            componentSnapshot.bounds = component->getBounds();

            if (auto* slider = dynamic_cast<Slider*> (component))
            {
                componentSnapshot.slider = ComponentSnapshot::SliderProperties {
                    slider->getMinimum(),
                    slider->getMaximum(),
                    slider->getInterval(),
                    slider->getSkewFactor(),
                    sliderStyleToString (slider->getSliderStyle()),
                    textBoxPositionToString (slider->getTextBoxPosition()),
                    slider->isTextBoxEditable(),
                    slider->getTextBoxWidth(),
                    slider->getTextBoxHeight()
                };
            }

            snapshot.components.push_back (std::move (componentSnapshot));
        }
    }

    return snapshot;
}

Result GuiDocumentAdapter::validate (const SliderDraft& draft) const
{
    if (document.getComponentLayout() == nullptr)
        return Result::fail ("The GUI document has no component layout.");

    if (draft.name.trim().isEmpty())
        return Result::fail ("The slider name must not be empty.");

    if (draft.memberName.trim().isEmpty())
        return Result::fail ("The slider member name must not be empty.");

    if (! std::isfinite (draft.minimum) || ! std::isfinite (draft.maximum) || ! std::isfinite (draft.interval))
        return Result::fail ("Slider range values must be finite.");

    if (draft.minimum >= draft.maximum)
        return Result::fail ("The slider minimum must be less than its maximum.");

    if (draft.interval < 0.0)
        return Result::fail ("The slider interval must not be negative.");

    if (draft.placement.size.x <= 0 || draft.placement.size.y <= 0)
        return Result::fail ("The slider size must be positive.");

    const Rectangle<int> componentBounds { 0, 0, document.getInitialWidth(), document.getInitialHeight() };

    if (! componentBounds.contains (resolveBounds (draft.placement)))
        return Result::fail ("The slider must fit inside the GUI component.");

    return Result::ok();
}

ApplyResult GuiDocumentAdapter::addSlider (const SliderDraft& draft, const String& transactionName)
{
    if (auto validation = validate (draft); validation.failed())
        return { validation };

    document.beginTransaction (transactionName);
    return addSliderWithoutStartingTransaction (draft);
}

BatchApplyResult GuiDocumentAdapter::addSliders (const std::vector<SliderDraft>& drafts, const String& transactionName)
{
    if (drafts.empty())
        return { Result::fail ("At least one Slider is required."), {} };

    for (const auto& draft : drafts)
        if (auto validation = validate (draft); validation.failed())
            return { validation, {} };

    document.beginTransaction (transactionName);
    std::vector<ApplyResult> results;
    results.reserve (drafts.size());

    for (const auto& draft : drafts)
    {
        auto result = addSliderWithoutStartingTransaction (draft);

        if (! result.wasApplied())
        {
            document.getUndoManager().undoCurrentTransactionOnly();
            return { result.status, {} };
        }

        results.push_back (std::move (result));
    }

    return { Result::ok(), std::move (results) };
}

ApplyResult GuiDocumentAdapter::addSliderWithoutStartingTransaction (const SliderDraft& draft)
{

    auto* layout = document.getComponentLayout();
    jassert (layout != nullptr);

    Slider slider (draft.name);
    slider.setRange (draft.minimum, draft.maximum, draft.interval);

    switch (draft.style)
    {
        case SliderStyle::rotaryHorizontalVerticalDrag:
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            break;
    }

    switch (draft.textBoxPosition)
    {
        case SliderTextBoxPosition::none:
            slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 80, 20);
            break;
    }

    slider.getProperties().set ("memberName", draft.memberName);

    const auto bounds = resolveBounds (draft.placement);
    RelativePositionedRectangle position;
    position.rect = PositionedRectangle (String (bounds.getX()) + " "
                                           + String (bounds.getY()) + " "
                                           + String (bounds.getWidth()) + " "
                                           + String (bounds.getHeight()));
    ComponentTypeHandler::setComponentPosition (&slider, position, layout);

    auto* handler = ComponentTypeHandler::getHandlerFor (slider);

    if (handler == nullptr)
        return { Result::fail ("No GUI Editor handler is registered for Slider.") };

    std::unique_ptr<XmlElement> xml (handler->createXmlFor (&slider, layout));
    if (auto* added = layout->addComponentFromXml (*xml, true))
    {
        layout->getSelectedSet().selectOnly (added);
        return { Result::ok(), ComponentTypeHandler::getComponentId (added), added->getBounds() };
    }

    return { Result::fail ("The Slider could not be restored into the GUI document.") };
}

Result GuiDocumentAdapter::undoCurrentAiTransaction()
{
    if (document.getUndoManager().undoCurrentTransactionOnly())
        return Result::ok();

    return Result::fail ("There is no current AI edit transaction to undo.");
}

Rectangle<int> GuiDocumentAdapter::resolveBounds (const ComponentPlacement& placement) const
{
    return resolveComponentPlacementBounds (placement,
                                            { 0, 0, document.getInitialWidth(), document.getInitialHeight() });
}
}
