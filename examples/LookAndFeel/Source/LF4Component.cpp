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

//[Headers] You can add your own extra header files here...
//[/Headers]

#include "LF4Component.h"


//[MiscUserDefs] You can add your own user definitions and misc code here...
//[/MiscUserDefs]

//==============================================================================
LF4Component::LF4Component ()
{
    //[Constructor_pre] You can add your own custom stuff here..
    //[/Constructor_pre]

    setLookAndFeel (&projectDefaultLookAndFeel);
    slider1.reset (new juce::Slider (juce::String()));
    addAndMakeVisible (slider1.get());
    slider1->setRange (0, 10, 0);
    slider1->setSliderStyle (juce::Slider::LinearHorizontal);
    slider1->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    slider1->addListener (this);

    slider1->setBounds (40, 32, 152, 40);

    slider2.reset (new juce::Slider (juce::String()));
    addAndMakeVisible (slider2.get());
    slider2->setRange (0, 10, 0);
    slider2->setSliderStyle (juce::Slider::Rotary);
    slider2->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    slider2->addListener (this);

    slider2->setBounds (216, 16, 72, 80);

    button1.reset (new juce::TextButton (juce::String()));
    addAndMakeVisible (button1.get());
    button1->setButtonText (TRANS ("new button"));
    button1->addListener (this);

    button1->setBounds (320, 40, 150, 24);


    //[UserPreSize]
    //[/UserPreSize]

    setSize (600, 400);


    //[Constructor] You can add your own custom stuff here..
    //[/Constructor]
}

LF4Component::~LF4Component()
{
    //[Destructor_pre]. You can add your own custom destruction code here..
    //[/Destructor_pre]

    setLookAndFeel (nullptr);
    slider1 = nullptr;
    slider2 = nullptr;
    button1 = nullptr;


    //[Destructor]. You can add your own custom destruction code here..
    //[/Destructor]
}

//==============================================================================
void LF4Component::paint (juce::Graphics& g)
{
    //[UserPrePaint] Add your own custom painting code here..
    //[/UserPrePaint]

    g.fillAll (juce::Colour (0xff323e44));

    //[UserPaint] Add your own custom painting code here..
    //[/UserPaint]
}

void LF4Component::resized()
{
    //[UserPreResize] Add your own custom resize code here..
    //[/UserPreResize]

    //[UserResized] Add your own custom resize handling here..
    //[/UserResized]
}

void LF4Component::sliderValueChanged (juce::Slider* sliderThatWasMoved)
{
    //[UsersliderValueChanged_Pre]
    //[/UsersliderValueChanged_Pre]

    if (sliderThatWasMoved == slider1.get())
    {
        //[UserSliderCode_slider1] -- add your slider handling code here..
        //[/UserSliderCode_slider1]
    }
    else if (sliderThatWasMoved == slider2.get())
    {
        //[UserSliderCode_slider2] -- add your slider handling code here..
        //[/UserSliderCode_slider2]
    }

    //[UsersliderValueChanged_Post]
    //[/UsersliderValueChanged_Post]
}

void LF4Component::buttonClicked (juce::Button* buttonThatWasClicked)
{
    //[UserbuttonClicked_Pre]
    //[/UserbuttonClicked_Pre]

    if (buttonThatWasClicked == button1.get())
    {
        //[UserButtonCode_button1] -- add your button handler code here..
        //[/UserButtonCode_button1]
    }

    //[UserbuttonClicked_Post]
    //[/UserbuttonClicked_Post]
}



//[MiscUserCode] You can add your own definitions of your custom methods or any other code here...
//[/MiscUserCode]


//==============================================================================
#if 0
/*  -- Projucer information section --

    This is where the Projucer stores the metadata that describe this GUI layout, so
    make changes in here at your peril!

BEGIN_JUCER_METADATA

<JUCER_COMPONENT documentType="Component" className="LF4Component" componentName=""
                 parentClasses="public juce::Component" constructorParams="" variableInitialisers=""
                 lookAndFeel="juce::LookAndFeel_V4" snapPixels="8" snapActive="1"
                 snapShown="1" overlayOpacity="0.330" fixedSize="0" initialWidth="600"
                 initialHeight="400">
  <BACKGROUND backgroundColour="ff323e44"/>
  <SLIDER name="" id="2570ee7bdfc4a2bb" memberName="slider1" virtualName=""
          explicitFocusOrder="0" pos="40 32 152 40" min="0.0" max="10.0"
          int="0.0" style="LinearHorizontal" textBoxPos="TextBoxBelow"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1.0"
          needsCallback="1" filmstripImage="" filmstripFrames="1" filmstripVertical="1"/>
  <SLIDER name="" id="b613c94e12d844a1" memberName="slider2" virtualName=""
          explicitFocusOrder="0" pos="216 16 72 80" min="0.0" max="10.0"
          int="0.0" style="Rotary" textBoxPos="TextBoxBelow" textBoxEditable="1"
          textBoxWidth="80" textBoxHeight="20" skewFactor="1.0" needsCallback="1"
          filmstripImage="" filmstripFrames="1" filmstripVertical="1"/>
  <TEXTBUTTON name="" id="b8430bf3bb10d660" memberName="button1" virtualName=""
              explicitFocusOrder="0" pos="320 40 150 24" buttonText="new button"
              connectedEdges="0" needsCallback="1" radioGroupId="0"/>
</JUCER_COMPONENT>

END_JUCER_METADATA
*/
#endif


//[EndFile] You can add extra defines here...
//[/EndFile]

