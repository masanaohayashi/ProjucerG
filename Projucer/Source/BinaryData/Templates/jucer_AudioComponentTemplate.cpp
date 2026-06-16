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

    // Make sure you set the size of the component after
    // you add any child components.
    setSize (800, 600);

    //[Constructor] You can add your own custom stuff here..

    // Some platforms require permissions to open input channels so request that here
    if (juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio)
        && ! juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
    {
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio,
                                           [&] (bool granted) { setAudioChannels (granted ? 2 : 0, 2); });
    }
    else
    {
        // Specify the number of input and output channels that we want to open
        setAudioChannels (2, 2);
    }
    //[/Constructor]
}

%%content_component_class%%::~%%content_component_class%%()
{
    //[Destructor_pre]. You can add your own custom destruction code here..
    //[/Destructor_pre]

    //[Destructor]. You can add your own custom destruction code here..

    // This shuts down the audio device and clears the audio source.
    shutdownAudio();
    //[/Destructor]
}

//==============================================================================
//[MiscUserCode] You can add your own definitions of your custom methods or any other code here...

void %%content_component_class%%::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    // This function will be called when the audio device is started, or when
    // its settings (i.e. sample rate, block size, etc) are changed.

    // You can use this function to initialise any resources you might need,
    // but be careful - it will be called on the audio thread, not the GUI thread.

    // For more details, see the help for AudioProcessor::prepareToPlay()
}

void %%content_component_class%%::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Your audio-processing code goes here!

    // For more details, see the help for AudioProcessor::getNextAudioBlock()

    // Right now we are not producing any data, in which case we need to clear the buffer
    // (to prevent the output of random noise)
    bufferToFill.clearActiveBufferRegion();
}

void %%content_component_class%%::releaseResources()
{
    // This will be called when the audio device stops, or when it is being
    // restarted due to a setting change.

    // For more details, see the help for AudioProcessor::releaseResources()
}

//[/MiscUserCode]

//==============================================================================
void %%content_component_class%%::paint (juce::Graphics& g)
{
    //[UserPrePaint] Add your own custom painting code here..
    //[/UserPrePaint]

    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    // You can add your drawing code here!

    //[UserPaint] Add your own custom painting code here..
    //[/UserPaint]
}

void %%content_component_class%%::resized()
{
    //[UserPreResize] Add your own custom resize code here..
    //[/UserPreResize]

    // This is called when the MainContentComponent is resized.
    // If you add any child components, this is where you should
    // update their positions.

    //[UserResized] Add your own custom resize handling here..
    //[/UserResized]
}

//==============================================================================
#if 0
/*  -- Projucer information section --

    This is where the Projucer stores the metadata that describe this GUI layout, so
    make changes in here at your peril!

BEGIN_JUCER_METADATA

<JUCER_COMPONENT documentType="Component" className="%%content_component_class%%"
                 componentName="" parentClasses="public juce::AudioAppComponent"
                 constructorParams="" variableInitialisers="" snapPixels="8"
                 snapActive="1" snapShown="1" overlayOpacity="0.330"
                 fixedSize="0" initialWidth="800" initialHeight="600"/>

END_JUCER_METADATA
*/
#endif

//[EndFile] You can add extra defines here...
//[/EndFile]
