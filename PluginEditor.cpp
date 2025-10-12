#include "PluginEditor.h"

#include "PluginProcessor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , processorRef(p)
{
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this]() { loadJSFXFile(); };

    addAndMakeVisible(unloadButton);
    unloadButton.onClick = [this]() { unloadJSFXFile(); };

    // Wet amount slider
    addAndMakeVisible(wetLabel);
    wetLabel.setText("Dry/Wet", juce::dontSendNotification);
    wetLabel.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(wetSlider);
    wetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    wetSlider.setRange(0.0, 1.0, 0.01);
    wetSlider.setValue(processorRef.getWetAmount(), juce::dontSendNotification);
    wetSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    wetSlider.onValueChange = [this]() { processorRef.setWetAmount(wetSlider.getValue()); };

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setText("No JSFX loaded", juce::dontSendNotification);

    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&parameterContainer, false);

    setSize(700, 500);

    rebuildParameterSliders();
    startTimer(200);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    stopTimer();
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    juce::String statusText = "No JSFX loaded";
    if (!processorRef.getCurrentJSFXName().isEmpty())
    {
        statusText = "Loaded: "
                   + processorRef.getCurrentJSFXName()
                   + " ("
                   + juce::String(processorRef.getNumActiveParameters())
                   + " parameters)";
    }
    statusLabel.setText(statusText, juce::dontSendNotification);

    // Update wet slider if it changed elsewhere
    if (std::abs(wetSlider.getValue() - processorRef.getWetAmount()) > 0.001)
        wetSlider.setValue(processorRef.getWetAmount(), juce::dontSendNotification);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void AudioPluginAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    auto buttonArea = bounds.removeFromTop(40);
    buttonArea.reduce(5, 5);
    loadButton.setBounds(buttonArea.removeFromLeft(100));
    buttonArea.removeFromLeft(5);
    unloadButton.setBounds(buttonArea.removeFromLeft(100));
    buttonArea.removeFromLeft(10);
    wetLabel.setBounds(buttonArea.removeFromLeft(30));
    buttonArea.removeFromLeft(5);
    wetSlider.setBounds(buttonArea.removeFromLeft(150));

    statusLabel.setBounds(bounds.removeFromTop(30));
    viewport.setBounds(bounds);
}

void AudioPluginAudioProcessorEditor::loadJSFXFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select a JSFX file to load...",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*"
    );

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(
        chooserFlags,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file != juce::File{})
            {
                if (processorRef.loadJSFX(file))
                {
                    rebuildParameterSliders();
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Error",
                        "Failed to load JSFX file: " + file.getFullPathName()
                    );
                }
            }
        }
    );
}

void AudioPluginAudioProcessorEditor::unloadJSFXFile()
{
    processorRef.unloadJSFX();
    rebuildParameterSliders();
}

void AudioPluginAudioProcessorEditor::rebuildParameterSliders()
{
    parameterSliders.clear();

    int numParams = processorRef.getNumActiveParameters();

    for (int i = 0; i < numParams; ++i)
    {
        auto* slider = new ParameterSlider(processorRef, i);
        parameterSliders.add(slider);
        parameterContainer.addAndMakeVisible(slider);
    }

    int totalHeight = numParams * 40;
    parameterContainer.setSize(viewport.getMaximumVisibleWidth(), totalHeight);

    int y = 0;
    for (auto* slider : parameterSliders)
    {
        slider->setBounds(0, y, viewport.getMaximumVisibleWidth() - 20, 35);
        y += 40;
    }
}
