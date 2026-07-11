/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   See LICENSE.md for the terms that apply to this repository.

  ==============================================================================
*/

#pragma once

#include "jucer_AutomationTypes.h"

class JucerDocument;

namespace ProjucerAutomation
{
class GuiDocumentAdapter
{
public:
    explicit GuiDocumentAdapter (JucerDocument&);

    GuiDocumentSnapshot createSnapshot() const;
    std::vector<ComponentTypeDescriptor> createComponentTypeCatalog() const;
    Image createComponentPreview (const ComponentDraft&) const;
    Result validate (const ComponentDraft&) const;
    BatchApplyResult addComponents (const std::vector<ComponentDraft>&, const String& transactionName);
    Result validate (const SliderDraft&) const;
    ApplyResult addSlider (const SliderDraft&, const String& transactionName);
    BatchApplyResult addSliders (const std::vector<SliderDraft>&, const String& transactionName);
    Result undoCurrentAiTransaction();
    Result removeComponents (const std::vector<ApplyResult>&);

private:
    ApplyResult addComponentWithoutStartingTransaction (const ComponentDraft&);
    std::unique_ptr<Component> createConfiguredComponent (const ComponentDraft&) const;
    ApplyResult addSliderWithoutStartingTransaction (const SliderDraft&);
    Rectangle<int> resolveBounds (const ComponentPlacement&) const;

    JucerDocument& document;
};
}
