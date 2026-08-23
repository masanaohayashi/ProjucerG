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

#include "../../Application/jucer_Headers.h"
#include "jucer_EditingPanelBase.h"
#include "jucer_JucerDocumentEditor.h"

#include <cmath>

//==============================================================================
class EditingPanelBase::MagnifierComponent  : public Component
{
public:
    explicit MagnifierComponent (Component* c) : content (c)
    {
        addAndMakeVisible (content.get());
        updateBounds (content.get());
    }

    void childBoundsChanged (Component* child) override
    {
        updateBounds (child);
    }

    double getScaleFactor() const   { return scaleFactor; }

    void setScaleFactor (double newScale)
    {
        scaleFactor = newScale;
        content->setTransform (AffineTransform::scale ((float) scaleFactor));
    }

private:
    void updateBounds (Component* child)
    {
        auto childArea = getLocalArea (child, child->getLocalBounds());
        setSize (childArea.getWidth(), childArea.getHeight());
    }

    double scaleFactor = 1.0;
    std::unique_ptr<Component> content;
};

//==============================================================================
class ZoomingViewport   : public Viewport
{
public:
    explicit ZoomingViewport (EditingPanelBase* p) : panel (p)
    {
        setScrollOnDragMode (ScrollOnDragMode::never);
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel) override
    {
        if (e.mods.isCtrlDown() || e.mods.isAltDown() || e.mods.isCommandDown())
            mouseMagnify (e, 1.0f / (1.0f - wheel.deltaY));
        else
            Viewport::mouseWheelMove (e, wheel);
    }

    void mouseMagnify (const MouseEvent& e, float factor) override
    {
        panel->setZoom (panel->getZoom() * factor, e.x, e.y);
    }

    void dragKeyHeldDown (const bool isKeyDown)
    {
        if (isSpaceDown != isKeyDown)
        {
            isSpaceDown = isKeyDown;

            if (isSpaceDown)
            {
                auto dc = new DraggerOverlayComp();
                addAndMakeVisible (dc);
                dc->setBounds (getLocalBounds());
            }
            else
            {
                for (int i = getNumChildComponents(); --i >= 0;)
                    std::unique_ptr<DraggerOverlayComp> deleter (dynamic_cast<DraggerOverlayComp*> (getChildComponent (i)));
            }
        }
    }

private:
    EditingPanelBase* const panel;
    bool isSpaceDown = false;

    //==============================================================================
    class DraggerOverlayComp    : public Component
    {
    public:
        DraggerOverlayComp()
        {
            setMouseCursor (MouseCursor::DraggingHandCursor);
            setAlwaysOnTop (true);
        }

        void mouseDown (const MouseEvent&) override
        {
            if (Viewport* viewport = findParentComponentOfClass<Viewport>())
            {
                startX = viewport->getViewPositionX();
                startY = viewport->getViewPositionY();
            }
        }

        void mouseDrag (const MouseEvent& e) override
        {
            if (Viewport* viewport = findParentComponentOfClass<Viewport>())
                viewport->setViewPosition (jlimit (0, jmax (0, viewport->getViewedComponent()->getWidth() - viewport->getViewWidth()),
                                                   startX - e.getDistanceFromDragStartX()),
                                           jlimit (0, jmax (0, viewport->getViewedComponent()->getHeight() - viewport->getViewHeight()),
                                                   startY - e.getDistanceFromDragStartY()));
        }

    private:
        int startX, startY;
    };
};


//==============================================================================
EditingPanelBase::EditingPanelBase (JucerDocument& doc, Component* props, Component* editorComp)
    : document (doc),
      editor (editorComp),
      propsPanel (props)
{
    addAndMakeVisible (viewport = new ZoomingViewport (this));
    addAndMakeVisible (propsPanel);

    viewport->setViewedComponent (magnifier = new MagnifierComponent (editor));
    viewport->addMouseListener (this, true);

    touchScrollX.addListener (this);
    touchScrollY.addListener (this);
    touchScrollX.behaviour.setMinimumVelocity (60.0);
    touchScrollY.behaviour.setMinimumVelocity (60.0);
}

EditingPanelBase::~EditingPanelBase()
{
    touchScrollX.removeListener (this);
    touchScrollY.removeListener (this);
    viewport->removeMouseListener (this);
    deleteAllChildren();
}

void EditingPanelBase::resized()
{
    const int contentW = jmax (1, getWidth() - 260);

    propsPanel->setBounds (contentW + 4, 4, jmax (100, getWidth() - contentW - 8), getHeight() - 8);

    viewport->setBounds (4, 4, contentW - 8, getHeight() - 8);

    if (document.isFixedSize())
        editor->setSize (jmax (document.getInitialWidth(),
                               roundToInt ((viewport->getWidth() - viewport->getScrollBarThickness()) / getZoom())),
                         jmax (document.getInitialHeight(),
                               roundToInt ((viewport->getHeight() - viewport->getScrollBarThickness()) / getZoom())));
    else
        editor->setSize (viewport->getWidth(),
                         viewport->getHeight());
}

void EditingPanelBase::paint (Graphics& g)
{
    g.fillAll (findColour (secondaryBackgroundColourId));
}

void EditingPanelBase::visibilityChanged()
{
    if (isVisible())
    {
        updatePropertiesList();

        if (Component* p = getParentComponent())
        {
            resized();

            if (JucerDocumentEditor* const cdh = dynamic_cast<JucerDocumentEditor*> (p->getParentComponent()))
                cdh->setViewportToLastPos (viewport, *this);

            resized();
        }
    }
    else
    {
        if (Component* p = getParentComponent())
            if (JucerDocumentEditor* const cdh = dynamic_cast<JucerDocumentEditor*> (p->getParentComponent()))
                cdh->storeLastViewportPos (viewport, *this);
    }

    editor->setVisible (isVisible());
}

double EditingPanelBase::getZoom() const
{
    return magnifier->getScaleFactor();
}

void EditingPanelBase::setZoom (double newScale)
{
    setZoom (jlimit (1.0 / 8.0, 16.0, newScale),
             viewport->getWidth() / 2,
             viewport->getHeight() / 2);
}

void EditingPanelBase::setZoom (double newScale, int anchorX, int anchorY)
{
    const Point<float> viewportPoint ((float) anchorX, (float) anchorY);
    setZoomKeepingPoint (newScale,
                         editor->getLocalPoint (viewport, viewportPoint),
                         viewportPoint);
}

void EditingPanelBase::xyToTargetXY (int& x, int& y) const
{
    Point<int> pos (editor->getLocalPoint (this, Point<int> (x, y)));
    x = pos.getX();
    y = pos.getY();
}

void EditingPanelBase::dragKeyHeldDown (bool isKeyDown)
{
    ((ZoomingViewport*) viewport)->dragKeyHeldDown (isKeyDown);
}

//==============================================================================
bool EditingPanelBase::handleTouchPinchMouseDown (const MouseEvent& event)
{
    if (isDuplicateTouchGestureEvent (event, TouchGestureEventType::mouseDown))
        return true;

    if (suppressTouchUntilAllReleased)
        return true;

    stopTouchScroll();
    return updateTouchGesture();
}

bool EditingPanelBase::handleTouchPinchMouseDrag (const MouseEvent& event)
{
    if (isDuplicateTouchGestureEvent (event, TouchGestureEventType::mouseDrag))
        return true;

    return updateTouchGesture() || suppressTouchUntilAllReleased;
}

bool EditingPanelBase::handleTouchPinchMouseUp (const MouseEvent& event)
{
    if (isDuplicateTouchGestureEvent (event, TouchGestureEventType::mouseUp))
        return true;

    if (! suppressTouchUntilAllReleased)
        return false;

    Point<float> first, second;

    if (getActiveTouchPair (first, second) < 2)
    {
        if (touchPinchActive)
            endTouchScroll();

        touchPinchActive = false;
        touchPinchStartDistance = 0.0f;
        touchPinchStartZoom = 1.0;
        touchPinchAnchor = {};
    }

    if (Desktop::getInstance().getNumDraggingMouseSources() == 0)
        resetTouchGesture();

    return true;
}

void EditingPanelBase::mouseDown (const MouseEvent& event)
{
    handleTouchPinchMouseDown (event);
}

void EditingPanelBase::mouseDrag (const MouseEvent& event)
{
    handleTouchPinchMouseDrag (event);
}

void EditingPanelBase::mouseUp (const MouseEvent& event)
{
    handleTouchPinchMouseUp (event);
}

int EditingPanelBase::getActiveTouchPair (Point<float>& first, Point<float>& second) const
{
    auto& desktop = Desktop::getInstance();
    int numTouches = 0;

    for (int i = 0; i < desktop.getNumDraggingMouseSources(); ++i)
    {
        auto* source = desktop.getDraggingMouseSource (i);

        if (source == nullptr || ! source->isTouch())
            continue;

        auto* component = source->getComponentUnderMouse();

        if (component == nullptr || (component != viewport && ! viewport->isParentOf (component)))
            continue;

        const auto position = viewport->getLocalPoint (nullptr, source->getScreenPosition());

        if (numTouches == 0)
            first = position;
        else if (numTouches == 1)
            second = position;

        ++numTouches;
    }

    return numTouches;
}

void EditingPanelBase::cancelActiveTouchInteractions()
{
    auto& desktop = Desktop::getInstance();

    for (int i = 0; i < desktop.getNumDraggingMouseSources(); ++i)
    {
        auto* source = desktop.getDraggingMouseSource (i);

        if (source == nullptr || ! source->isTouch())
            continue;

        auto* component = source->getComponentUnderMouse();

        if (component == nullptr || (component != viewport && ! viewport->isParentOf (component)))
            continue;

        for (auto* current = component; current != nullptr && current != viewport;
             current = current->getParentComponent())
        {
            if (auto* cancellable = dynamic_cast<ProjucerTouchPinchGestureCancellable*> (current))
            {
                cancellable->cancelTouchInteraction();
                break;
            }
        }
    }
}

bool EditingPanelBase::updateTouchGesture()
{
    Point<float> first, second;

    if (getActiveTouchPair (first, second) < 2)
        return false;

    const auto centre = (first + second) * 0.5f;
    const auto distance = first.getDistanceFrom (second);

    if (! touchPinchActive)
    {
        if (! std::isfinite (distance) || distance <= 0.0f)
            return true;

        cancelActiveTouchInteractions();
        touchPinchActive = true;
        suppressTouchUntilAllReleased = true;
        touchPinchStartDistance = distance;
        touchPinchStartZoom = getZoom();
        touchPinchAnchor = editor->getLocalPoint (viewport, centre);
        beginTouchScroll (centre);
        return true;
    }

    const auto scrollOffset = centre - touchScrollStartCentre;
    touchScrollX.drag (scrollOffset.x);
    touchScrollY.drag (scrollOffset.y);

    if (std::isfinite (distance)
         && distance > 0.0f
         && std::isfinite (touchPinchStartDistance)
         && touchPinchStartDistance > 0.0f
         && std::isfinite (touchPinchStartZoom)
         && touchPinchStartZoom > 0.0)
    {
        const auto factor = distance / touchPinchStartDistance;

        if (std::isfinite (factor) && factor > 0.0f)
            setZoomKeepingPoint (touchPinchStartZoom * factor,
                                 touchPinchAnchor,
                                 centre);
    }

    return true;
}

void EditingPanelBase::resetTouchGesture()
{
    touchPinchActive = false;
    suppressTouchUntilAllReleased = false;
    touchPinchStartDistance = 0.0f;
    touchPinchStartZoom = 1.0;
    touchPinchAnchor = {};
}

void EditingPanelBase::beginTouchScroll (Point<float> centre)
{
    ignoreTouchScrollChanges = true;
    touchScrollX.setPosition (0.0);
    touchScrollY.setPosition (0.0);
    lastTouchScrollOffset = {};
    touchScrollRemainder = {};
    touchScrollStartCentre = centre;
    touchScrollX.beginDrag();
    touchScrollY.beginDrag();
}

void EditingPanelBase::endTouchScroll()
{
    ignoreTouchScrollChanges = false;
    touchScrollX.endDrag();
    touchScrollY.endDrag();
}

void EditingPanelBase::stopTouchScroll()
{
    touchScrollX.setPosition (touchScrollX.getPosition());
    touchScrollY.setPosition (touchScrollY.getPosition());
}

void EditingPanelBase::positionChanged (ProjucerTouchScrollPosition&, double)
{
    const Point<float> offset ((float) touchScrollX.getPosition(),
                               (float) touchScrollY.getPosition());
    const auto delta = offset - lastTouchScrollOffset;
    lastTouchScrollOffset = offset;

    if (ignoreTouchScrollChanges || delta == Point<float>())
        return;

    touchScrollRemainder += delta;
    const auto deltaPixels = touchScrollRemainder.roundToInt();
    touchScrollRemainder -= deltaPixels.toFloat();

    if (deltaPixels != Point<int>())
        viewport->setViewPosition (viewport->getViewPosition() - deltaPixels);
}

bool EditingPanelBase::isDuplicateTouchGestureEvent (const MouseEvent& event,
                                                     TouchGestureEventType type)
{
    if (! event.source.isTouch())
        return false;

    const auto screenPosition = event.source.getScreenPosition();
    const bool duplicate = lastTouchGestureEventType == type
                        && lastTouchGestureSource == event.source.getIndex()
                        && lastTouchGestureEventTime == event.eventTime
                        && lastTouchGestureScreenPosition == screenPosition;

    lastTouchGestureEventType = type;
    lastTouchGestureSource = event.source.getIndex();
    lastTouchGestureEventTime = event.eventTime;
    lastTouchGestureScreenPosition = screenPosition;
    return duplicate;
}

void EditingPanelBase::setZoomKeepingPoint (double newScale,
                                            Point<float> editorPoint,
                                            Point<float> viewportPoint)
{
    newScale = jlimit (1.0 / 8.0, 16.0, newScale);

    magnifier->setScaleFactor (newScale);
    resized();

    jassert (viewport != nullptr);
    const auto transformedPoint = viewport->getLocalPoint (editor, editorPoint);
    const auto newViewPosition = viewport->getViewPosition().toFloat()
                               + transformedPoint
                               - viewportPoint;

    viewport->setViewPosition (newViewPosition.roundToInt());
}
