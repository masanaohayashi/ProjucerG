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
    Result validate (const SliderDraft&) const;
    ApplyResult addSlider (const SliderDraft&, const String& transactionName);
    Result undoCurrentAiTransaction();

private:
    Rectangle<int> resolveBounds (const ComponentPlacement&) const;

    JucerDocument& document;
};
}
