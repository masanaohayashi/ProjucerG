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

#include "../jucer_JucerDocument.h"
#include "jucer_ComponentLayoutEditor.h"
#include "jucer_TouchPinchGesture.h"
class LayoutPropsPanel;

using ProjucerTouchScrollPosition = AnimatedPosition<AnimatedPositionBehaviours::ContinuousWithMomentum>;

//==============================================================================
/**
    Base class for the layout and graphics panels - this takes care of arranging
    the properties panel and managing the viewport for the content.

*/
class EditingPanelBase  : public Component,
                          public ProjucerTouchPinchGestureTarget,
                          private ProjucerTouchScrollPosition::Listener
{
public:
    //==============================================================================
    EditingPanelBase (JucerDocument& document,
                      Component* propsPanel,
                      Component* editorComp);

    ~EditingPanelBase() override;

    //==============================================================================
    void resized() override;
    void paint (Graphics& g) override;
    void visibilityChanged() override;

    virtual void updatePropertiesList() = 0;

    virtual Rectangle<int> getComponentArea() const = 0;

    double getZoom() const;
    void setZoom (double newScale);
    void setZoom (double newScale, int anchorX, int anchorY);

    // convert a pos relative to this component into a pos on the editor
    void xyToTargetXY (int& x, int& y) const;

    void dragKeyHeldDown (bool isKeyDown);

    class MagnifierComponent;

protected:
    bool handleTouchPinchMouseDown (const MouseEvent&) override;
    bool handleTouchPinchMouseDrag (const MouseEvent&) override;
    bool handleTouchPinchMouseUp (const MouseEvent&) override;

    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;

    int getActiveTouchPair (Point<float>& first, Point<float>& second) const;
    void cancelActiveTouchInteractions();
    bool updateTouchGesture();
    void resetTouchGesture();
    void beginTouchScroll (Point<float> centre);
    void endTouchScroll();
    void stopTouchScroll();
    void positionChanged (ProjucerTouchScrollPosition&, double) override;
    enum class TouchGestureEventType { none, mouseDown, mouseDrag, mouseUp };
    bool isDuplicateTouchGestureEvent (const MouseEvent&, TouchGestureEventType);
    void setZoomKeepingPoint (double newScale,
                              Point<float> editorPoint,
                              Point<float> viewportPoint);

    JucerDocument& document;

    Viewport* viewport;
    MagnifierComponent* magnifier;
    Component* editor;
    Component* propsPanel;

    bool touchPinchActive = false;
    bool suppressTouchUntilAllReleased = false;
    float touchPinchStartDistance = 0.0f;
    double touchPinchStartZoom = 1.0;
    Point<float> touchPinchAnchor;
    Point<float> touchScrollStartCentre;
    Point<float> lastTouchScrollOffset;
    Point<float> touchScrollRemainder;
    ProjucerTouchScrollPosition touchScrollX, touchScrollY;
    bool ignoreTouchScrollChanges = false;
    TouchGestureEventType lastTouchGestureEventType = TouchGestureEventType::none;
    int lastTouchGestureSource = -1;
    Time lastTouchGestureEventTime;
    Point<float> lastTouchGestureScreenPosition;
};
