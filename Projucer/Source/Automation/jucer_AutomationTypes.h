/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   See LICENSE.md for the terms that apply to this repository.

  ==============================================================================
*/

#pragma once

namespace ProjucerAutomation
{
enum class PlacementAnchor
{
    componentCentre,
    componentTopLeft
};

enum class SliderStyle
{
    rotaryHorizontalVerticalDrag
};

enum class SliderTextBoxPosition
{
    none
};

struct ComponentPlacement
{
    PlacementAnchor anchor = PlacementAnchor::componentCentre;
    Point<int> offset;
    Point<int> size { 120, 120 };
};

inline Rectangle<int> resolveComponentPlacementBounds (const ComponentPlacement& placement,
                                                       const Rectangle<int>& componentBounds)
{
    if (placement.anchor == PlacementAnchor::componentTopLeft)
        return { componentBounds.getX() + placement.offset.x,
                 componentBounds.getY() + placement.offset.y,
                 placement.size.x,
                 placement.size.y };

    Point<int> anchorPoint;

    switch (placement.anchor)
    {
        case PlacementAnchor::componentCentre:
            anchorPoint = componentBounds.getCentre();
            break;
        case PlacementAnchor::componentTopLeft:
            anchorPoint = componentBounds.getPosition();
            break;
    }

    return Rectangle<int> (placement.size.x, placement.size.y).withCentre (anchorPoint + placement.offset);
}

struct SliderDraft
{
    String name;
    String memberName;
    double minimum = 0.0;
    double maximum = 1.0;
    double interval = 0.0;
    SliderStyle style = SliderStyle::rotaryHorizontalVerticalDrag;
    SliderTextBoxPosition textBoxPosition = SliderTextBoxPosition::none;
    ComponentPlacement placement;
};

struct ComponentSnapshot
{
    int64 id = 0;
    String type;
    String name;
    String memberName;
    Rectangle<int> bounds;

    struct SliderProperties
    {
        double minimum = 0.0;
        double maximum = 1.0;
        double interval = 0.0;
        double skewFactor = 1.0;
        String style;
        String textBoxPosition;
        bool textBoxEditable = false;
        int textBoxWidth = 0;
        int textBoxHeight = 0;
    };

    std::optional<SliderProperties> slider;
};

struct GuiDocumentSnapshot
{
    File projectFile;
    File guiFile;
    Rectangle<int> componentBounds;
    int snapGridSize = 8;
    bool snapActive = true;
    bool snapShown = true;
    std::vector<ComponentSnapshot> components;
};

struct ApplyResult
{
    ApplyResult (Result statusToUse, int64 componentIdToUse = 0, Rectangle<int> boundsToUse = {})
        : status (std::move (statusToUse)), componentId (componentIdToUse), bounds (boundsToUse)
    {
    }

    Result status;
    int64 componentId = 0;
    Rectangle<int> bounds;

    bool wasApplied() const { return status.wasOk() && componentId != 0; }
};

struct BatchApplyResult
{
    Result status;
    std::vector<ApplyResult> components;

    bool wasApplied() const { return status.wasOk() && ! components.empty(); }
};
}
