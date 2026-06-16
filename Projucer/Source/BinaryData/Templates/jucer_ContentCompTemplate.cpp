%%include_corresponding_header%%

//[MiscUserDefs] You can add your own user definitions and misc code here...
//[/MiscUserDefs]

//==============================================================================
%%content_component_class%%::%%content_component_class%%()
{
    //[Constructor_pre] You can add your own custom stuff here..
    //[/Constructor_pre]

    //[UserPreSize]
    //[/UserPreSize]

    setSize (600, 400);

    //[Constructor] You can add your own custom stuff here..
    //[/Constructor]
}

%%content_component_class%%::~%%content_component_class%%()
{
    //[Destructor_pre]. You can add your own custom destruction code here..
    //[/Destructor_pre]

    //[Destructor]. You can add your own custom destruction code here..
    //[/Destructor]
}

//==============================================================================
void %%content_component_class%%::paint (juce::Graphics& g)
{
    //[UserPrePaint] Add your own custom painting code here..
    //[/UserPrePaint]

    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setFont (juce::FontOptions (16.0f));
    g.setColour (juce::Colours::white);
    g.drawText ("Hello World!", getLocalBounds(), juce::Justification::centred, true);

    //[UserPaint] Add your own custom painting code here..
    //[/UserPaint]
}

void %%content_component_class%%::resized()
{
    //[UserPreResize] Add your own custom resize code here..
    //[/UserPreResize]

    // This is called when the %%content_component_class%% is resized.
    // If you add any child components, this is where you should
    // update their positions.

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

<JUCER_COMPONENT documentType="Component" className="%%content_component_class%%"
                 componentName="" parentClasses="public juce::Component"
                 constructorParams="" variableInitialisers="" snapPixels="8"
                 snapActive="1" snapShown="1" overlayOpacity="0.330"
                 fixedSize="0" initialWidth="600" initialHeight="400"/>

END_JUCER_METADATA
*/
#endif

//[EndFile] You can add extra defines here...
//[/EndFile]
