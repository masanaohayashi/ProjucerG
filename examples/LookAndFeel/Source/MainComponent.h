/*
  ==============================================================================

  This is an automatically generated GUI class created by the Projucer!

  Be careful when adding custom code to these files, as only the code within
  the "//[xyz]" and "//[/xyz]" sections will be retained when the file is loaded
  and re-saved.

  Created with Projucer version: 8.0.13

  ------------------------------------------------------------------------------

  The Projucer is part of the JUCE library.
  Copyright (c) - Raw Material Software Limited.

  ==============================================================================
*/

#pragma once

//[Headers]     -- You can add your own extra header files here --
#include <JuceHeader.h>
//[/Headers]



//==============================================================================
/**
                                                                    //[Comments]
    An auto-generated component, created by the Projucer.

    Describe your class and how it works here!
                                                                    //[/Comments]
*/
class MainComponent  : public juce::Component,
                       public juce::Slider::Listener,
                       public juce::Button::Listener
{
public:
    //==============================================================================
    MainComponent ();
    ~MainComponent() override;

    //==============================================================================
    //[UserMethods]     -- You can add your own custom methods in this section.
    //[/UserMethods]

    void paint (juce::Graphics& g) override;
    void resized() override;
    void sliderValueChanged (juce::Slider* sliderThatWasMoved) override;
    void buttonClicked (juce::Button* buttonThatWasClicked) override;



private:
    //[UserVariables]   -- You can add your own custom variables in this section.
    //[/UserVariables]

    //==============================================================================
    juce::LookAndFeel_V1 projectDefaultLookAndFeel;
    juce::LookAndFeel_V4 tabbedComponentLookAndFeel;
    std::unique_ptr<juce::TabbedComponent> tabbedComponent;
    std::unique_ptr<juce::Slider> juce__slider;
    juce::LookAndFeel_V4 juce__slider2LookAndFeel;
    std::unique_ptr<juce::Slider> juce__slider2;
    juce::LookAndFeel_V4 juce__textButtonLookAndFeel;
    std::unique_ptr<juce::TextButton> juce__textButton;
    juce::LookAndFeel_V1 juce__textButton2LookAndFeel;
    std::unique_ptr<juce::TextButton> juce__textButton2;
    juce::LookAndFeel_V2 juce__textButton3LookAndFeel;
    std::unique_ptr<juce::TextButton> juce__textButton3;
    std::unique_ptr<juce::ToggleButton> juce__toggleButton;
    juce::LookAndFeel_V4 juce__toggleButton2LookAndFeel;
    std::unique_ptr<juce::ToggleButton> juce__toggleButton2;


    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

//[EndFile] You can add extra defines here...
//[/EndFile]

