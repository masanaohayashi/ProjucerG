/*
  ==============================================================================

    IfwTabbedLookAndFeel.h

    Header-only test LookAndFeel containing only the TabbedComponent-related
    behaviour extracted from IfwLookAndFeel.

  ==============================================================================
*/

#pragma once

#include "JuceHeader.h"

class IfwTabbedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    int getTabButtonBestWidth (juce::TabBarButton& button, int) override
    {
        if (auto* bar = button.findParentComponentOfClass<juce::TabbedButtonBar>())
            return bar->getNumTabs() > 0 ? bar->getWidth() / bar->getNumTabs() : 120;

        return 120;
    }

    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) override
    {
    }

    void drawTabButtonText (juce::TabBarButton& button,
                            juce::Graphics& g,
                            bool,
                            bool) override
    {
        const auto tabHeight = button.getHeight();
        auto fontSize = static_cast<float> (tabHeight - 3);
        fontSize = juce::jlimit (10.0f, 20.0f, fontSize);

        g.setFont (juce::Font (juce::FontOptions (fontSize, juce::Font::plain)));
        g.setColour (button.findColour (juce::TabbedButtonBar::tabTextColourId));
        g.drawFittedText (button.getButtonText(),
                          button.getLocalBounds().reduced (4),
                          juce::Justification::centred,
                          1);
    }
};
