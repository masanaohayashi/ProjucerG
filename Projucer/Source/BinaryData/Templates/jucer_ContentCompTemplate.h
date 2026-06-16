#pragma once

//[Headers]     -- You can add your own extra header files here --
%%include_juce%%
//[/Headers]

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class %%content_component_class%%  : public juce::Component
{
public:
    //==============================================================================
    %%content_component_class%%();
    ~%%content_component_class%%() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    //[UserMethods]     -- You can add your own custom methods in this section.
    //[/UserMethods]

private:
    //[UserVariables]   -- You can add your own custom variables in this section.
    //[/UserVariables]

    //==============================================================================
    // Your private member variables go here...


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (%%content_component_class%%)
};

//[EndFile] You can add extra defines here...
//[/EndFile]
