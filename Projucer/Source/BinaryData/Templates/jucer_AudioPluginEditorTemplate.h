/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

//[Headers]     -- You can add your own extra header files here --
%%editor_headers%%
//[/Headers]

//==============================================================================
/**
*/
class %%editor_class_name%%  : public juce::AudioProcessorEditor
{
public:
    %%editor_class_name%% (%%filter_class_name%%&);
    ~%%editor_class_name%%() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    //[UserMethods]     -- You can add your own custom methods in this section.
    //[/UserMethods]

private:
    //[UserVariables]   -- You can add your own custom variables in this section.
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    %%filter_class_name%%& audioProcessor;
    //[/UserVariables]

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (%%editor_class_name%%)
};

//[EndFile] You can add extra defines here...
//[/EndFile]
