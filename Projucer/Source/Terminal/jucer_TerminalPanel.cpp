#include "../Application/jucer_Headers.h"
#include "jucer_TerminalPanel.h"

class TerminalPanel::TabLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void setColours (juce::Colour background, juce::Colour text, juce::Colour indicator)
    {
        backgroundColour = background;
        textColour = text;
        indicatorColour = indicator;
    }

    void drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                        bool isMouseOver, bool isMouseDown) override
    {
        auto area = button.getActiveArea();
        g.setColour (backgroundColour);
        g.fillRect (area);

        // Keep inactive tab labels readable against the shared panel background.
        // The front-tab indicator already communicates which tab is active.
        const float alpha = button.isFrontTab() || isMouseOver || isMouseDown ? 1.0f : 0.9f;
        g.setColour (textColour.withMultipliedAlpha (alpha));
        g.drawFittedText (button.getButtonText(), button.getTextArea(),
                          juce::Justification::centred, 1);

        if (button.isFrontTab())
        {
            g.setColour (indicatorColour);
            g.fillRect (area.removeFromBottom (2));
        }
    }

    int getTabButtonBestWidth (juce::TabBarButton& button, int) override
    {
        if (auto* bar = button.findParentComponentOfClass<juce::TabbedButtonBar>())
            return bar->getWidth() / juce::jmax (1, bar->getNumTabs());

        return 120;
    }

    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics& g,
                                       int width, int height) override
    {
        // This component is layered above inactive tab buttons and behind the
        // front tab. Filling its entire bounds would hide their labels.
        juce::ignoreUnused (g, width, height);
    }

private:
    juce::Colour backgroundColour;
    juce::Colour textColour;
    juce::Colour indicatorColour;
};

TerminalPanel::TerminalPanel (const juce::File& wd)
    : workingDirectory (wd),
      tabLookAndFeel (std::make_unique<TabLookAndFeel>())
{
    setOpaque (true);

    addAndMakeVisible (tabs);
    tabs.setOutline (0);
    tabs.getTabbedButtonBar().setLookAndFeel (tabLookAndFeel.get());
    lookAndFeelChanged();

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
    tabs.getTabbedButtonBar().setLookAndFeel (nullptr);
}

void TerminalPanel::addTerminal()
{
    const auto name = "Terminal " + juce::String (nextTerminalNumber++);

    tabs.addTab (name,
                 findColour (backgroundColourId),
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

bool TerminalPanel::runCommand (const juce::String& commandLine)
{
    auto* terminal = getCurrentTerminal();

    if (terminal == nullptr)
        return false;

    terminal->sendCommandLine (commandLine);
    focusCurrentTerminal();
    return true;
}

bool TerminalPanel::runCommandAndWait (const juce::String& commandLine,
                                       juce::String& output,
                                       int timeoutMs,
                                       std::atomic<bool>& cancelled)
{
    auto* terminal = getCurrentTerminal();

    if (terminal == nullptr)
        return false;

    juce::MessageManager::callAsync ([this]
    {
        focusCurrentTerminal();
    });

    return terminal->runCommandAndWait (commandLine, output, timeoutMs, cancelled);
}

void TerminalPanel::paint (juce::Graphics& g)
{
    g.fillAll (findColour (backgroundColourId));

    // A hairline along the top, so the panel reads as a separate region even
    // before the user notices the resizer sitting on it.
    g.setColour (findColour (juce::CodeEditorComponent::defaultTextColourId).withAlpha (0.2f));
    g.fillRect (0, 0, getWidth(), 1);
}

void TerminalPanel::lookAndFeelChanged()
{
    if (tabLookAndFeel == nullptr)
        return;

    const auto background = findColour (backgroundColourId);
    tabLookAndFeel->setColours (background,
                                findColour (defaultTextColourId),
                                findColour (defaultButtonBackgroundColourId));
    tabs.setColour (juce::TabbedComponent::backgroundColourId, background);

    for (int i = 0; i < tabs.getNumTabs(); ++i)
        tabs.setTabBackgroundColour (i, background);

    repaint();
    tabs.repaint();
}

void TerminalPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop (1);

    auto buttonRow = area.removeFromTop (24).removeFromRight (56);
    closeButton.setBounds (buttonRow.removeFromRight (28).reduced (2));
    addButton.setBounds (buttonRow.reduced (2));

    tabs.setBounds (getLocalBounds().withTrimmedTop (1));
}
