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
GuiDocumentAdapter::GuiDocumentAdapter (JucerDocument& documentToUse)
    : document (documentToUse)
{
}

GuiDocumentSnapshot GuiDocumentAdapter::createSnapshot() const
{
    GuiDocumentSnapshot snapshot;
    snapshot.guiFile = document.getCppFile();
    snapshot.componentBounds = { 0, 0, document.getInitialWidth(), document.getInitialHeight() };

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

            snapshot.components.push_back ({ ComponentTypeHandler::getComponentId (component),
                                             handler->getClassName (component),
                                             component->getName(),
                                             layout->getComponentMemberVariableName (component),
                                             component->getBounds() });
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
    document.beginTransaction (transactionName);

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
