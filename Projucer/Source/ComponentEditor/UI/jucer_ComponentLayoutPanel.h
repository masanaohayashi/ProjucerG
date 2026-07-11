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

#include "../../Automation/jucer_AutomationTypes.h"
#include "../../Automation/jucer_GuiDocumentAdapter.h"
#include "jucer_ComponentLayoutEditor.h"
#include "jucer_EditingPanelBase.h"

//==============================================================================
class ComponentLayoutPanel  : public EditingPanelBase
{
public:
    //==============================================================================
    ComponentLayoutPanel (JucerDocument& doc, ComponentLayout& l)
        : EditingPanelBase (doc,
                            new LayoutPropsPanel (doc, l),
                            new ComponentLayoutEditor (doc, l)),
          layout (l)
    {
    }

    ~ComponentLayoutPanel() override
    {
        liveEditOverlay.reset();
        deleteAllChildren();
    }

    void updatePropertiesList() override
    {
        ((LayoutPropsPanel*) propsPanel)->updateList();
    }

    void resized() override
    {
        EditingPanelBase::resized();
        refreshLiveEditOverlay();
    }

    Rectangle<int> getComponentArea() const override
    {
        return ((ComponentLayoutEditor*) editor)->getComponentArea();
    }

    Image createComponentSnapshot() const
    {
        return ((ComponentLayoutEditor*) editor)->createComponentLayerSnapshot();
    }

    void showLiveEditPreview (const ProjucerAutomation::SliderDraft& draftToUse)
    {
        showLiveEditPreview (std::vector<ProjucerAutomation::SliderDraft> { draftToUse });
    }

    void showLiveEditPreview (const std::vector<ProjucerAutomation::SliderDraft>& draftsToUse)
    {
        std::vector<ProjucerAutomation::ComponentDraft> components;
        components.reserve (draftsToUse.size());

        for (const auto& slider : draftsToUse)
        {
            ProjucerAutomation::ComponentDraft component;
            component.type = "SLIDER";
            component.name = slider.name;
            component.memberName = slider.memberName;
            component.placement = slider.placement;
            component.properties.set ("min", slider.minimum);
            component.properties.set ("max", slider.maximum);
            component.properties.set ("int", slider.interval);
            component.properties.set ("style", "RotaryHorizontalVerticalDrag");
            component.properties.set ("textBoxPos", "NoTextBox");
            components.push_back (std::move (component));
        }

        showLiveEditPreview (components);
    }

    void showLiveEditPreview (const std::vector<ProjucerAutomation::ComponentDraft>& draftsToUse)
    {
        liveEditDeletionIds.clear();
        liveEditDrafts = draftsToUse;
        liveEditPreviewVisible = true;
        liveEditPaused = false;
        liveEditApplied = false;
        liveEditState = "previewing";
        liveEditCompletionReason.clear();
        liveEditStatusText = "AI editing: previewing " + String (liveEditDrafts.size()) + " component(s)";
        refreshLiveEditPreviewImages();

        if (liveEditOverlay == nullptr)
        {
            liveEditOverlay.reset (new LiveEditOverlay (*this));
            addAndMakeVisible (liveEditOverlay.get());
        }

        liveEditOverlay->syncFromState();
        liveEditOverlay->toFront (false);
        liveEditOverlay->grabKeyboardFocus();
        resized();
        repaint();
    }

    void showLiveEditDeletionPreview (const std::vector<int64>& componentIds)
    {
        liveEditDrafts.clear();
        liveEditDeletionIds = componentIds;
        liveEditPreviewVisible = true;
        liveEditPaused = false;
        liveEditApplied = false;
        liveEditState = "previewing";
        liveEditCompletionReason.clear();
        liveEditStatusText = "AI editing: previewing deletion of " + String (liveEditDeletionIds.size()) + " component(s)";
        liveEditPreviewImages.clear();

        if (liveEditOverlay == nullptr)
        {
            liveEditOverlay.reset (new LiveEditOverlay (*this));
            addAndMakeVisible (liveEditOverlay.get());
        }

        liveEditOverlay->syncFromState();
        liveEditOverlay->toFront (false);
        liveEditOverlay->grabKeyboardFocus();
        resized();
        repaint();
    }

    bool cancelLiveEditPreview (String reason)
    {
        if (! liveEditPreviewVisible || liveEditApplied)
            return false;

        liveEditPreviewVisible = false;
        liveEditPaused = false;
        liveEditApplied = false;
        liveEditStatusText = "AI edit cancelled";
        liveEditState = "cancelled";
        liveEditCompletionReason = std::move (reason);
        liveEditDrafts.clear();
        liveEditDeletionIds.clear();
        liveEditResult = { Result::ok(), {} };
        liveEditPreviewImages.clear();

        if (liveEditOverlay != nullptr)
            liveEditOverlay->syncFromState();

        repaint();
        return true;
    }

    void cancelLiveEditPreviewFromEscape()
    {
        cancelLiveEditPreview ("user_escape");
    }

    bool canCancelLiveEditPreview() const noexcept
    {
        return liveEditPreviewVisible && ! liveEditApplied;
    }

    void toggleLiveEditPause()
    {
        if (! liveEditPreviewVisible || liveEditApplied)
            return;

        liveEditPaused = ! liveEditPaused;
        liveEditState = liveEditPaused ? "paused" : "previewing";
        liveEditCompletionReason.clear();
        liveEditStatusText = liveEditPaused ? "AI editing: paused"
                                            : "AI editing: previewing " + String (liveEditDrafts.size()) + " component(s)";

        if (liveEditOverlay != nullptr)
            liveEditOverlay->syncFromState();

        repaint();
    }

    void applyLiveEditPreview()
    {
        if (! liveEditPreviewVisible || liveEditApplied || liveEditPaused)
            return;

        if (liveEditDrafts.empty() && liveEditDeletionIds.empty())
            return;

        ProjucerAutomation::GuiDocumentAdapter adapter (document);
        Result deletionResult = Result::ok();

        if (! liveEditDeletionIds.empty())
        {
            deletionResult = adapter.deleteComponents (liveEditDeletionIds, "AI Edit - Delete Components");
            liveEditApplied = deletionResult.wasOk();
        }
        else
        {
            liveEditResult = adapter.addComponents (liveEditDrafts, "AI Edit - Add Components");
            liveEditApplied = liveEditResult.wasApplied();
        }

        if (liveEditApplied)
        {
            liveEditStatusText = "AI editing: applied - undo available";
            liveEditState = "applied";
            liveEditCompletionReason = "applied";
            liveEditPreviewVisible = false;
            layout.getSelectedSet().deselectAll();

            if (liveEditDeletionIds.empty())
                for (int i = 0; i < layout.getNumComponents(); ++i)
                    if (auto* addedComponent = layout.getComponent (i))
                        for (const auto& result : liveEditResult.components)
                            if (ComponentTypeHandler::getComponentId (addedComponent) == result.componentId)
                                layout.getSelectedSet().addToSelection (addedComponent);

            updatePropertiesList();
        }
        else
        {
            liveEditStatusText = "AI editing failed: " + (! liveEditDeletionIds.empty()
                                                              ? deletionResult.getErrorMessage()
                                                              : liveEditResult.status.getErrorMessage());
            liveEditState = "failed";
            liveEditCompletionReason = "apply_failed";
        }

        if (liveEditOverlay != nullptr)
            liveEditOverlay->syncFromState();

        repaint();
    }

    void undoLiveEditPreview()
    {
        if (! liveEditPreviewVisible)
            return;

        if (liveEditApplied)
        {
            ProjucerAutomation::GuiDocumentAdapter adapter (document);
            const auto result = ! liveEditDeletionIds.empty() ? adapter.undoCurrentAiTransaction()
                                                               : adapter.removeComponents (liveEditResult.components);

            if (result.failed())
            {
                liveEditStatusText = "AI undo failed: " + result.getErrorMessage();
                liveEditState = "failed";
                liveEditCompletionReason = "undo_failed";

                if (liveEditOverlay != nullptr)
                    liveEditOverlay->syncFromState();

                repaint();
                return;
            }

            liveEditApplied = false;
        }

        cancelLiveEditPreview ("user_undo");
    }

    bool isLiveEditPreviewVisible() const noexcept
    {
        return liveEditPreviewVisible;
    }

    bool isLiveEditPaused() const noexcept
    {
        return liveEditPaused;
    }

    bool isLiveEditApplied() const noexcept
    {
        return liveEditApplied;
    }

    String getLiveEditState() const
    {
        return liveEditState;
    }

    String getLiveEditCompletionReason() const
    {
        return liveEditCompletionReason;
    }

    String getLiveEditStatusText() const
    {
        return liveEditStatusText;
    }

    Rectangle<int> getLiveEditPreviewBounds (size_t index) const
    {
        if (! liveEditPreviewVisible || index >= liveEditDrafts.size())
            return {};

        const auto editorBounds = ProjucerAutomation::resolveComponentPlacementBounds (liveEditDrafts[index].placement,
                                                                                        { 0, 0,
                                                                                          document.getInitialWidth(),
                                                                                          document.getInitialHeight() });

        return editorBoundsToPanelBounds (editorBounds);
    }

    Image getLiveEditPreviewImage (size_t index) const
    {
        return index < liveEditPreviewImages.size() ? liveEditPreviewImages[index] : Image{};
    }

    bool isLiveEditDeletionPreview() const noexcept { return ! liveEditDeletionIds.empty(); }

    std::vector<Rectangle<int>> getLiveEditDeletionBounds() const
    {
        std::vector<Rectangle<int>> bounds;
        for (const auto componentId : liveEditDeletionIds)
            if (auto* component = layout.findComponentWithId (componentId))
                bounds.push_back (editorBoundsToPanelBounds (component->getBounds()));
        return bounds;
    }

    size_t getNumLiveEditPreviews() const noexcept
    {
        return liveEditDrafts.size();
    }

    Rectangle<int> editorBoundsToPanelBounds (const Rectangle<int>& editorBounds) const
    {
        if (auto* componentEditor = dynamic_cast<ComponentLayoutEditor*> (editor))
        {
            // Component bounds are relative to the design surface (the
            // sub-component holder), not to ComponentLayoutEditor itself.
            // Fixed-size documents centre that surface inside the editor, so
            // include its origin before converting into panel coordinates.
            const auto componentArea = componentEditor->getComponentArea();
            return getLocalArea (componentEditor,
                                 editorBounds.translated (componentArea.getX(),
                                                          componentArea.getY()));
        }

        return editorBounds;
    }

    ComponentLayout& layout;

private:
    class LiveEditOverlay  : public Component
    {
    public:
        explicit LiveEditOverlay (ComponentLayoutPanel& owner)
            : panel (owner)
        {
            setInterceptsMouseClicks (false, true);
            setWantsKeyboardFocus (true);

            pauseButton.onClick = [this] { panel.toggleLiveEditPause(); };
            applyButton.onClick = [this] { panel.applyLiveEditPreview(); };
            undoButton.onClick = [this] { panel.undoLiveEditPreview(); };
            cancelButton.onClick = [this] { panel.cancelLiveEditPreview ("user_cancel"); };

            addAndMakeVisible (pauseButton);
            addAndMakeVisible (applyButton);
            addAndMakeVisible (undoButton);
            addAndMakeVisible (cancelButton);
        }

        void syncFromState()
        {
            setVisible (panel.isLiveEditPreviewVisible());
            pauseButton.setButtonText (panel.isLiveEditPaused() ? "Resume" : "Pause");
            applyButton.setEnabled (panel.isLiveEditPreviewVisible() && ! panel.isLiveEditPaused() && ! panel.isLiveEditApplied());
            undoButton.setEnabled (panel.isLiveEditPreviewVisible() && panel.isLiveEditApplied());
            cancelButton.setButtonText ("Cancel");
            repaint();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (10);
            auto buttonRow = area.removeFromTop (32);
            auto rightButtons = buttonRow.removeFromRight (380);

            cancelButton.setBounds (rightButtons.removeFromRight (90));
            rightButtons.removeFromRight (8);
            undoButton.setBounds (rightButtons.removeFromRight (120));
            rightButtons.removeFromRight (8);
            applyButton.setBounds (rightButtons.removeFromRight (90));
            rightButtons.removeFromRight (8);
            pauseButton.setBounds (rightButtons.removeFromRight (90));
        }

        void paint (Graphics& g) override
        {
            if (! panel.isLiveEditPreviewVisible())
                return;

            g.fillAll (Colours::transparentBlack);

            auto banner = getLocalBounds().removeFromTop (54);
            g.setColour (Colours::black.withAlpha (0.75f));
            g.fillRoundedRectangle (banner.toFloat(), 8.0f);

            g.setColour (Colours::white);
            g.setFont (Font (15.0f, Font::bold));
            g.drawText (panel.getLiveEditStatusText(), banner.reduced (14, 8), Justification::centredLeft, false);

            for (size_t i = 0; i < panel.getNumLiveEditPreviews(); ++i)
            {
                auto previewBounds = panel.getLiveEditPreviewBounds (i);
                auto previewImage = panel.getLiveEditPreviewImage (i);
                if (! previewBounds.isEmpty() && previewImage.isValid())
                {
                    g.setOpacity (panel.isLiveEditPaused() ? 0.35f : 0.6f);
                    g.drawImageWithin (previewImage,
                                       previewBounds.getX(), previewBounds.getY(),
                                       previewBounds.getWidth(), previewBounds.getHeight(),
                                       RectanglePlacement::stretchToFit);
                    g.setOpacity (1.0f);
                }
            }

            for (const auto bounds : panel.getLiveEditDeletionBounds())
            {
                g.setColour (Colours::red.withAlpha (panel.isLiveEditPaused() ? 0.18f : 0.32f));
                g.fillRect (bounds);
                g.setColour (Colours::red.withAlpha (0.9f));
                g.drawRect (bounds, 3);
                g.drawLine ((float) bounds.getX(), (float) bounds.getY(),
                            (float) bounds.getRight(), (float) bounds.getBottom(), 3.0f);
                g.drawLine ((float) bounds.getRight(), (float) bounds.getY(),
                            (float) bounds.getX(), (float) bounds.getBottom(), 3.0f);
            }
        }

        bool keyPressed (const KeyPress& key) override
        {
            if (key.isKeyCode (KeyPress::escapeKey) && panel.canCancelLiveEditPreview())
            {
                panel.cancelLiveEditPreviewFromEscape();
                return true;
            }

            return false;
        }

    private:
        ComponentLayoutPanel& panel;
        TextButton pauseButton { "Pause" };
        TextButton applyButton { "Apply" };
        TextButton undoButton { "Undo Edit" };
        TextButton cancelButton { "Cancel" };
    };

    class LayoutPropsPanel  : public Component,
                              private ChangeListener
    {
    public:
        LayoutPropsPanel (JucerDocument& doc, ComponentLayout& l)
            : document (doc), layout (l)
        {
            layout.getSelectedSet().addChangeListener (this);
            addAndMakeVisible (propsPanel);
        }

        ~LayoutPropsPanel() override
        {
            layout.getSelectedSet().removeChangeListener (this);
            clear();
        }

        void resized() override
        {
            propsPanel.setBounds (4, 4, getWidth() - 8, getHeight() - 8);
        }

        void clear()
        {
            propsPanel.clear();
        }

        void updateList()
        {
            clear();

            auto numSelected = layout.getSelectedSet().getNumSelected();

            if (numSelected > 0) // xxx need to cope with multiple
            {
                if (auto* comp = layout.getSelectedSet().getSelectedItem (0))
                    if (auto* type = ComponentTypeHandler::getHandlerFor (*comp))
                        type->addPropertiesToPropertyPanel (comp, document, propsPanel, numSelected > 1);
            }
        }

    private:
        void changeListenerCallback (ChangeBroadcaster*) override
        {
            updateList();
        }

        JucerDocument& document;
        ComponentLayout& layout;
        PropertyPanel propsPanel;
    };

    void refreshLiveEditOverlay()
    {
        if (liveEditOverlay != nullptr)
        {
            liveEditOverlay->syncFromState();
            liveEditOverlay->setBounds (getLocalBounds());
            liveEditOverlay->toFront (false);
        }
    }

    void refreshLiveEditPreviewImages()
    {
        liveEditPreviewImages.clear();

        if (! liveEditPreviewVisible || liveEditDrafts.empty())
            return;

        if (dynamic_cast<ComponentLayoutEditor*> (editor) == nullptr)
            return;

        for (const auto& draft : liveEditDrafts)
            liveEditPreviewImages.push_back (ProjucerAutomation::GuiDocumentAdapter (document).createComponentPreview (draft));
    }

    std::unique_ptr<LiveEditOverlay> liveEditOverlay;
    std::vector<ProjucerAutomation::ComponentDraft> liveEditDrafts;
    std::vector<int64> liveEditDeletionIds;
    bool liveEditPreviewVisible = false;
    bool liveEditPaused = false;
    bool liveEditApplied = false;
    String liveEditState = "idle";
    String liveEditCompletionReason;
    ProjucerAutomation::BatchApplyResult liveEditResult { Result::ok(), {} };
    String liveEditStatusText = "AI編集は停止中";
    std::vector<Image> liveEditPreviewImages;
};
