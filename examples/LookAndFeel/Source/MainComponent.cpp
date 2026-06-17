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

#include "MainComponent.h"
#include "LF1Component.h"
#include "LF2Component.h"
#include "LF3Component.h"
#include "LF4Component.h"


//[MiscUserDefs] You can add your own user definitions and misc code here...
//[/MiscUserDefs]

//==============================================================================
MainComponent::MainComponent ()
{
    //[Constructor_pre] You can add your own custom stuff here..
    //[/Constructor_pre]

    setLookAndFeel (&projectDefaultLookAndFeel);
    tabbedComponent.reset (new juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop));
    addAndMakeVisible (tabbedComponent.get());
    tabbedComponent->setTabBarDepth (30);
    tabbedComponent->addTab (TRANS ("LookAndFeelV1"), juce::Colours::lightgrey, new LF1Component(), true);
    tabbedComponent->addTab (TRANS ("LookAndFeelV2"), juce::Colours::lightgrey, new LF2Component(), true);
    tabbedComponent->addTab (TRANS ("LookAndFeelV3"), juce::Colours::lightgrey, new LF3Component(), true);
    tabbedComponent->addTab (TRANS ("LookAndFeel V4"), juce::Colours::lightgrey, new LF4Component(), true);
    tabbedComponent->setCurrentTabIndex (0);

    tabbedComponent->setBounds (0, 8, 600, 272);

    juce__slider.reset (new juce::Slider ("new slider"));
    addAndMakeVisible (juce__slider.get());
    juce__slider->setRange (0, 10, 0);
    juce__slider->setSliderStyle (juce::Slider::LinearHorizontal);
    juce__slider->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 80, 20);
    juce__slider->addListener (this);

    juce__slider->setBounds (64, 328, 150, 24);

    juce__slider2.reset (new juce::Slider ("new slider"));
    addAndMakeVisible (juce__slider2.get());
    juce__slider2->setRange (0, 10, 0);
    juce__slider2->setSliderStyle (juce::Slider::Rotary);
    juce__slider2->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    juce__slider2->addListener (this);

    juce__slider2->setBounds (280, 304, 104, 64);


    //[UserPreSize]
    //[/UserPreSize]

    setSize (600, 400);


    //[Constructor] You can add your own custom stuff here..
    //[/Constructor]
}

MainComponent::~MainComponent()
{
    //[Destructor_pre]. You can add your own custom destruction code here..
    //[/Destructor_pre]

    setLookAndFeel (nullptr);
    tabbedComponent = nullptr;
    juce__slider = nullptr;
    juce__slider2 = nullptr;


    //[Destructor]. You can add your own custom destruction code here..
    //[/Destructor]
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    //[UserPrePaint] Add your own custom painting code here..
    //[/UserPrePaint]

    g.fillAll (juce::Colour (0xff323e44));

    //[UserPaint] Add your own custom painting code here..
    //[/UserPaint]
}

void MainComponent::resized()
{
    //[UserPreResize] Add your own custom resize code here..
    //[/UserPreResize]

    //[UserResized] Add your own custom resize handling here..
    //[/UserResized]
}

void MainComponent::sliderValueChanged (juce::Slider* sliderThatWasMoved)
{
    //[UsersliderValueChanged_Pre]
    //[/UsersliderValueChanged_Pre]

    if (sliderThatWasMoved == juce__slider.get())
    {
        //[UserSliderCode_juce__slider] -- add your slider handling code here..
        //[/UserSliderCode_juce__slider]
    }
    else if (sliderThatWasMoved == juce__slider2.get())
    {
        //[UserSliderCode_juce__slider2] -- add your slider handling code here..
        //[/UserSliderCode_juce__slider2]
    }

    //[UsersliderValueChanged_Post]
    //[/UsersliderValueChanged_Post]
}



//[MiscUserCode] You can add your own definitions of your custom methods or any other code here...
//[/MiscUserCode]


//==============================================================================
#if 0
/*  -- Projucer information section --

    This is where the Projucer stores the metadata that describe this GUI layout, so
    make changes in here at your peril!

BEGIN_JUCER_METADATA

<JUCER_COMPONENT documentType="Component" className="MainComponent" componentName=""
                 parentClasses="public juce::Component" constructorParams="" variableInitialisers=""
                 lookAndFeel="juce::LookAndFeel_V3" snapPixels="8" snapActive="1"
                 snapShown="1" overlayOpacity="0.330" fixedSize="1" initialWidth="600"
                 initialHeight="400">
  <BACKGROUND backgroundColour="ff323e44"/>
  <TABBEDCOMPONENT name="" id="7438dfa499ef687a" memberName="tabbedComponent" virtualName=""
                   explicitFocusOrder="0" pos="0 8 600 272" orientation="top" tabBarDepth="30"
                   initialTab="0">
    <TAB name="LookAndFeelV1" colour="ffd3d3d3" useJucerComp="1" contentClassName=""
         constructorParams="" jucerComponentFile="LF1Component.cpp"/>
    <TAB name="LookAndFeelV2" colour="ffd3d3d3" useJucerComp="1" contentClassName=""
         constructorParams="" jucerComponentFile="LF2Component.cpp"/>
    <TAB name="LookAndFeelV3" colour="ffd3d3d3" useJucerComp="1" contentClassName=""
         constructorParams="" jucerComponentFile="LF3Component.cpp"/>
    <TAB name="LookAndFeel V4" colour="ffd3d3d3" useJucerComp="1" contentClassName=""
         constructorParams="" jucerComponentFile="LF4Component.cpp"/>
  </TABBEDCOMPONENT>
  <SLIDER name="new slider" id="680adf010a039988" memberName="juce__slider"
          virtualName="" explicitFocusOrder="0" pos="64 328 150 24" min="0.0"
          max="10.0" int="0.0" style="LinearHorizontal" textBoxPos="TextBoxLeft"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1.0"
          needsCallback="1" filmstripImage="" filmstripFrames="1" filmstripVertical="1"/>
  <SLIDER name="new slider" id="d4ac44703eba12fe" memberName="juce__slider2"
          virtualName="" explicitFocusOrder="0" pos="280 304 104 64" min="0.0"
          max="10.0" int="0.0" style="Rotary" textBoxPos="TextBoxBelow"
          textBoxEditable="1" textBoxWidth="80" textBoxHeight="20" skewFactor="1.0"
          needsCallback="1" filmstripImage="" filmstripFrames="1" filmstripVertical="1"/>
</JUCER_COMPONENT>

END_JUCER_METADATA
*/
#endif


//[EndFile] You can add extra defines here...
//[/EndFile]

