#include "PluginEditor.h"

#include "PluginProcessor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p)

    , processorRef(p)
{
    juce::ignoreUnused(processorRef);
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize(400, 300);
    startTimerHz(30); // Start a timer to update the UI at 30 FPS
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    if (init)
        return;

    init = true;
    auto* sxInstance = processorRef.getSXInstancePtr();
    sxInstance->DoUpdate((HWND)getTopLevelComponent()->getWindowHandle());
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    // JesusonicAPI.sx_deleteUI(sxUi);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    // g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    // g.setColour(juce::Colours::white);
    // g.setFont(15.0f);
    // g.drawFittedText("Hello World!", getLocalBounds(), juce::Justification::centred, 1);
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
}
