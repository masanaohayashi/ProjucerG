#include "../Application/jucer_Headers.h"
#include "jucer_TerminalPanel.h"

TerminalPanel::TerminalPanel (const juce::File& wd)
    : workingDirectory (wd)
{
    setOpaque (true);

    addAndMakeVisible (tabs);
    tabs.setOutline (0);
    tabs.setColour (juce::TabbedComponent::backgroundColourId, juce::Colours::black);

    addAndMakeVisible (addButton);
    addButton.setTooltip ("Open another terminal");
    addButton.onClick = [this] { addTerminal(); };

    addAndMakeVisible (closeButton);
    closeButton.setTooltip ("Close this terminal");
    closeButton.onClick = [this] { closeCurrentTerminal(); };

    addTerminal();
}

TerminalPanel::~TerminalPanel()
{
    // Clearing the tabs deletes the views, and each view's destructor hangs up
    // its shell. Doing it explicitly keeps the order obvious.
    tabs.clearTabs();
}

void TerminalPanel::addTerminal()
{
    const auto name = "Terminal " + juce::String (nextTerminalNumber++);

    tabs.addTab (name,
                 juce::Colours::black,
                 new TerminalView (workingDirectory),
                 true);

    tabs.setCurrentTabIndex (tabs.getNumTabs() - 1);
    focusCurrentTerminal();
}

void TerminalPanel::closeCurrentTerminal()
{
    if (tabs.getNumTabs() <= 1)
        return;                       // always leave the user one terminal

    tabs.removeTab (tabs.getCurrentTabIndex());
    focusCurrentTerminal();
}

TerminalView* TerminalPanel::getCurrentTerminal() const
{
    return dynamic_cast<TerminalView*> (tabs.getCurrentContentComponent());
}

void TerminalPanel::focusCurrentTerminal()
{
    if (auto* terminal = getCurrentTerminal(); terminal != nullptr && terminal->isShowing())
        terminal->grabKeyboardFocus();
}

void TerminalPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    // A hairline along the top, so the panel reads as a separate region even
    // before the user notices the resizer sitting on it.
    g.setColour (findColour (juce::CodeEditorComponent::defaultTextColourId).withAlpha (0.2f));
    g.fillRect (0, 0, getWidth(), 1);
}

void TerminalPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop (1);

    auto buttonRow = area.removeFromTop (24).removeFromRight (56);
    closeButton.setBounds (buttonRow.removeFromRight (28).reduced (2));
    addButton.setBounds (buttonRow.reduced (2));

    tabs.setBounds (getLocalBounds().withTrimmedTop (1));
}
