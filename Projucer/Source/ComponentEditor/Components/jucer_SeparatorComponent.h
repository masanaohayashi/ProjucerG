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
/** The splitter bar as it exists inside the Projucer.

    In the layout editor this only paints - the bar has to stay exactly where the
    user dropped it. "Test Component" reuses these same objects, so setLiveMode()
    switches on the dragging behaviour there.

    NOTE: getTravel(), getWantedSplitPosition(), setSplitPosition() and applyLayout()
    are mirrored by the JucerSeparatorBar class that SeparatorComponentHandler emits
    into generated code - tools/test_separator_layout.py fails if they drift apart.
*/
class SeparatorComponent  : public Component
{
public:
    /** What happens to the two halves when the parent component is resized. */
    enum ResizeMode
    {
        proportional = 0,   /**< both halves keep their share of the area. */
        keepBeforeFixed,    /**< the "before" half keeps its size, "after" absorbs the change. */
        keepAfterFixed      /**< the "after" half keeps its size, "before" absorbs the change. */
    };

    static StringArray getResizeModeNames()
    {
        return { "both resize proportionally", "keep before fixed", "keep after fixed" };
    }

    static String toString (ResizeMode m)
    {
        return m == keepBeforeFixed ? "before" : (m == keepAfterFixed ? "after" : "proportional");
    }

    static ResizeMode fromString (const String& s)
    {
        return s == "before" ? keepBeforeFixed : (s == "after" ? keepAfterFixed : proportional);
    }

    // (reusing a colour id the LookAndFeel already knows about - a private one would
    // assert in LookAndFeel::findColour when the property panel looks up its default)
    static constexpr int barColourId = ResizableWindow::backgroundColourId;

    SeparatorComponent()
    {
        setName ("separator");
    }

    bool isVertical() const noexcept        { return getWidth() <= getHeight(); }

    int64 beforeId = 0, afterId = 0;
    int minSizeBefore = 0, minSizeAfter = 0;
    ResizeMode resizeMode = proportional;

    //==============================================================================
    /** Switches on the dragging behaviour. The targets and the split position are
        picked up on the first parentSizeChanged(), because until the test component
        has laid itself out these components have no meaningful bounds yet.
    */
    void setLiveMode (bool shouldBeLive)
    {
        live = shouldBeLive;
        before = after = nullptr;
        splitInitialised = false;

        setMouseCursor (! live         ? MouseCursor::NormalCursor
                        : isVertical() ? MouseCursor::LeftRightResizeCursor
                                       : MouseCursor::UpDownResizeCursor);
    }

    /** Resolves the two targets from a list of candidate siblings. */
    void findTargetsIn (const Array<Component*>& candidates)
    {
        before = after = nullptr;

        for (auto* c : candidates)
        {
            auto id = c->getProperties() ["jucerCompId"].toString().getHexValue64();

            if (id != 0 && id == beforeId)  before = c;
            if (id != 0 && id == afterId)   after = c;
        }
    }

    /** Reads the split the user drew in the layout editor into proportion/fixedSize. */
    void captureDesignedSplit()
    {
        const auto area = getSplitArea();
        const auto travel = getTravel();

        if (travel <= 0)
            return;

        setSplitPosition (jlimit (0, travel, isVertical() ? getX() - area.getX()
                                                          : getY() - area.getY()),
                          travel);
    }

    double getProportion() const noexcept       { return proportion; }
    int getFixedSize() const noexcept           { return fixedSize; }

    //==============================================================================
    void paint (Graphics& g) override
    {
        auto background = findColour (barColourId);

        g.fillAll (isColourSpecified (barColourId) ? background : background.contrasting (0.1f));
        g.setColour (background.contrasting (0.4f));

        auto centre = getLocalBounds().getCentre();

        for (int i = -1; i <= 1; ++i)
        {
            auto dot = isVertical() ? Rectangle<int> (centre.x - 1, centre.y + i * 5 - 1, 2, 2)
                                    : Rectangle<int> (centre.x + i * 5 - 1, centre.y - 1, 2, 2);
            g.fillRect (dot);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (! live)
            return;

        dragStart = positionInParent (e);
        splitAtDragStart = getWantedSplitPosition (getTravel());
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (! live)
            return;

        const auto travel = getTravel();

        if (travel <= 0)
            return;

        setSplitPosition (jlimit (0, travel, splitAtDragStart + positionInParent (e) - dragStart), travel);
        applyLayout();
    }

    void parentSizeChanged() override
    {
        if (! live)
            return;

        if (! splitInitialised)
        {
            if (auto* parent = getParentComponent())
                findTargetsIn (parent->getChildren());

            // (the layout the user drew is only readable now that resized() has run)
            captureDesignedSplit();
            splitInitialised = true;
        }

        applyLayout();
    }

private:
    int positionInParent (const MouseEvent& e) const
    {
        auto* parent = getParentComponent();

        if (parent == nullptr)
            return isVertical() ? e.x : e.y;

        auto pos = e.getEventRelativeTo (parent).getPosition();
        return isVertical() ? pos.x : pos.y;
    }

    Rectangle<int> getSplitArea() const
    {
        auto area = getBounds();

        if (before != nullptr)  area = area.getUnion (before->getBounds());
        if (after != nullptr)   area = area.getUnion (after->getBounds());

        return area;
    }

    int getTravel() const
    {
        const auto vertical = isVertical();
        auto area = getSplitArea();

        return (vertical ? area.getWidth() : area.getHeight())
                 - (vertical ? getWidth() : getHeight());
    }

    int getWantedSplitPosition (int travel) const
    {
        if (resizeMode == keepBeforeFixed)  return fixedSize;
        if (resizeMode == keepAfterFixed)   return travel - fixedSize;

        return roundToInt (travel * proportion);
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

        const auto vertical = isVertical();
        const auto area = getSplitArea();
        const auto thickness = vertical ? getWidth() : getHeight();
        const auto travel = (vertical ? area.getWidth() : area.getHeight()) - thickness;

        if (travel <= 0)
            return;

        const auto minA = jlimit (0, travel, minSizeBefore);
        const auto minB = jlimit (0, travel - minA, minSizeAfter);

        const auto pos = jlimit (minA, travel - minB, getWantedSplitPosition (travel));
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

    bool live = false, splitInitialised = false;
    double proportion = 0.5;
    int fixedSize = 0, dragStart = 0, splitAtDragStart = 0;
    Component::SafePointer<Component> before, after;
};
