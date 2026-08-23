/*
  ============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2022 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

  ============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/** The editing panel which owns the shared two-finger gesture state. */
class ProjucerTouchPinchGestureTarget
{
public:
    virtual ~ProjucerTouchPinchGestureTarget() = default;

    virtual bool handleTouchPinchMouseDown (const MouseEvent&) = 0;
    virtual bool handleTouchPinchMouseDrag (const MouseEvent&) = 0;
    virtual bool handleTouchPinchMouseUp (const MouseEvent&) = 0;
};

//==============================================================================
/** A component which can close an interaction when a second finger takes over. */
class ProjucerTouchPinchGestureCancellable
{
public:
    virtual ~ProjucerTouchPinchGestureCancellable() = default;
    virtual void cancelTouchInteraction() = 0;
};

//==============================================================================
namespace ProjucerTouchPinchGesture
{
inline ProjucerTouchPinchGestureTarget* findTarget (Component& component)
{
    return component.findParentComponentOfClass<ProjucerTouchPinchGestureTarget>();
}

inline bool mouseDown (Component& component, const MouseEvent& event)
{
    if (! event.source.isTouch())
        return false;

    if (auto* target = findTarget (component))
        return target->handleTouchPinchMouseDown (event);

    return false;
}

inline bool mouseDrag (Component& component, const MouseEvent& event)
{
    if (! event.source.isTouch())
        return false;

    if (auto* target = findTarget (component))
        return target->handleTouchPinchMouseDrag (event);

    return false;
}

inline bool mouseUp (Component& component, const MouseEvent& event)
{
    if (! event.source.isTouch())
        return false;

    if (auto* target = findTarget (component))
        return target->handleTouchPinchMouseUp (event);

    return false;
}
}
