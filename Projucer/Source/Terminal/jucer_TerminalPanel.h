#pragma once

#include "jucer_TerminalView.h"

#include <atomic>

//==============================================================================
/**
    The dock at the bottom of the project window: a row of tabs, each holding
    one shell, plus buttons to open and close them.

    The panel owns the terminals but not its own placement - the height and the
    resizer live in ProjectContentComponent, next to the sidebar's, so that all
    of the window's layout arithmetic stays in one place.
*/
class TerminalPanel final : public juce::Component
{
public:
    explicit TerminalPanel (const juce::File& workingDirectory);
    ~TerminalPanel() override;

    void addTerminal();
    void closeCurrentTerminal();
    void focusCurrentTerminal();
    bool runCommand (const juce::String& commandLine);
    bool runCommandAndWait (const juce::String& commandLine,
                            juce::String& output,
                            int timeoutMs,
                            std::atomic<bool>& cancelled);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int defaultHeight = 220;
    static constexpr int minimumHeight = 80;

private:
    class TabLookAndFeel;

    TerminalView* getCurrentTerminal() const;

    juce::File workingDirectory;
    std::unique_ptr<TabLookAndFeel> tabLookAndFeel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TextButton addButton { "+" };
    juce::TextButton closeButton { "x" };
    int nextTerminalNumber = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerminalPanel)
};
