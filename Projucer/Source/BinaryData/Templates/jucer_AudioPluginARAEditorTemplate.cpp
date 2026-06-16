/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

%%editor_cpp_headers%%

//[MiscUserDefs] You can add your own user definitions and misc code here...
//[/MiscUserDefs]

//==============================================================================
%%editor_class_name%%::%%editor_class_name%% (%%filter_class_name%%& p)
    : AudioProcessorEditor (&p),
     #if JucePlugin_Enable_ARA
      AudioProcessorEditorARAExtension (&p),
     #endif
      audioProcessor (p)
{
    //[Constructor_pre] You can add your own custom stuff here..
    //[/Constructor_pre]

    //[UserPreSize]
    //[/UserPreSize]

   #if JucePlugin_Enable_ARA
    // ARA plugins must be resizable for proper view embedding
    setResizable (true, false);
   #endif

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 300);

    //[Constructor] You can add your own custom stuff here..
    //[/Constructor]
}

%%editor_class_name%%::~%%editor_class_name%%()
{
    //[Destructor_pre]. You can add your own custom destruction code here..
    //[/Destructor_pre]

    //[Destructor]. You can add your own custom destruction code here..
    //[/Destructor]
}

//==============================================================================
void %%editor_class_name%%::paint (juce::Graphics& g)
{
    //[UserPrePaint] Add your own custom painting code here..
    //[/UserPrePaint]

    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);

    //[UserPaint] Add your own custom painting code here..
    //[/UserPaint]
}

void %%editor_class_name%%::resized()
{
    //[UserPreResize] Add your own custom resize code here..
    //[/UserPreResize]

    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..

    //[UserResized] Add your own custom resize handling here..
    //[/UserResized]
}

//[MiscUserCode] You can add your own definitions of your custom methods or any other code here...
//[/MiscUserCode]


//==============================================================================
#if 0
/*  -- Projucer information section --

    This is where the Projucer stores the metadata that describe this GUI layout, so
    make changes in here at your peril!

BEGIN_JUCER_METADATA

<JUCER_COMPONENT documentType="Component" className="%%editor_class_name%%"
                 componentName=""
                 parentClasses="public juce::AudioProcessorEditor, public juce::AudioProcessorEditorARAExtension"
                 constructorParams="%%filter_class_name%%&amp; p"
                 variableInitialisers="AudioProcessorEditor (&amp;p), AudioProcessorEditorARAExtension (&amp;p), audioProcessor (p)"
                 snapPixels="8" snapActive="1" snapShown="1" overlayOpacity="0.330"
                 fixedSize="0" initialWidth="400" initialHeight="300"/>

END_JUCER_METADATA
*/
#endif

//[EndFile] You can add extra defines here...
//[/EndFile]
