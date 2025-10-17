#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "JsfxEditorWindow.h"
#include "JsfxLiceComponent.h"
#include "PluginProcessor.h"

class PersistentFileChooser;

//==============================================================================
// Custom DocumentWindow that handles close button properly
class IOMatrixWindow : public juce::DocumentWindow
{
public:
    IOMatrixWindow()
        : DocumentWindow("I/O Routing Matrix", juce::Colours::darkgrey, juce::DocumentWindow::closeButton)
    {
        setResizable(true, true);
        setResizeLimits(300, 200, 2000, 2000);
        setUsingNativeTitleBar(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IOMatrixWindow)
};

//==============================================================================
class ParameterSlider : public juce::Component
{
public:
    ParameterSlider(AudioPluginAudioProcessor& proc, int paramIndex)
        : processor(proc)
        , index(paramIndex)
    {
        auto paramID = juce::String("param") + juce::String(paramIndex);

        addAndMakeVisible(nameLabel);
        nameLabel.setJustificationType(juce::Justification::centredLeft);

        double minVal = 0.0, maxVal = 1.0, step = 0.0;
        bool hasRange = processor.getJSFXParameterRange(index, minVal, maxVal, step);

        // Check if it's an enum/choice parameter
        bool isEnum = processor.isJSFXParameterEnum(index);

        // Detect parameter type from min/max/step
        if (hasRange && minVal == 0.0 && maxVal == 1.0 && step == 1.0 && !isEnum)
        {
            // Boolean parameter - use toggle button
            controlType = ControlType::ToggleButton;
            toggleButton.setButtonText("");
            addAndMakeVisible(toggleButton);

            buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                processor.getAPVTS(),
                paramID,
                toggleButton
            );
        }
        else if (isEnum && hasRange)
        {
            // Enum/choice parameter - use combo box
            controlType = ControlType::ComboBox;
            addAndMakeVisible(comboBox);

            // Build combo box items from enum values
            int numItems = juce::roundToInt(maxVal - minVal) + 1;
            for (int i = 0; i < numItems; ++i)
            {
                double actualValue = minVal + i;
                juce::String itemText = processor.getJSFXParameterDisplayText(index, actualValue);
                comboBox.addItem(itemText, i + 1); // ComboBox item IDs start at 1
            }

            comboBoxAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                processor.getAPVTS(),
                paramID,
                comboBox
            );
        }
        else
        {
            // Numeric parameter - use slider
            controlType = ControlType::Slider;
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 100, 20);
            slider.setRange(0.0, 1.0, 0.001);
            addAndMakeVisible(slider);

            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.getAPVTS(),
                paramID,
                slider
            );

            if (hasRange)
            {
                // Check if it's an integer parameter
                bool isIntParam = (step >= 1.0);

                slider.textFromValueFunction =
                    [&proc = this->processor, idx = this->index, minVal, maxVal](double normalizedValue) -> juce::String
                {
                    double actualValue = minVal + normalizedValue * (maxVal - minVal);
                    return proc.getJSFXParameterDisplayText(idx, actualValue);
                };

                slider.valueFromTextFunction = [minVal, maxVal](const juce::String& text) -> double
                {
                    double actualValue = text.getDoubleValue();
                    if (maxVal > minVal)
                        return (actualValue - minVal) / (maxVal - minVal);
                    return 0.0;
                };

                // For integer parameters, set discrete interval
                if (isIntParam && maxVal > minVal)
                {
                    int numSteps = juce::roundToInt(maxVal - minVal);
                    if (numSteps > 0 && numSteps < 1000)
                        slider.setRange(0.0, 1.0, 1.0 / numSteps);
                }

                // Force slider to update its text box with the new formatting
                slider.updateText();
            }
        }

        updateFromProcessor();
    }

    void updateFromProcessor()
    {
        juce::String paramName = processor.getJSFXParameterName(index);
        nameLabel.setText(paramName, juce::dontSendNotification);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        nameLabel.setBounds(bounds.removeFromLeft(200));

        switch (controlType)
        {
        case ControlType::ToggleButton:
            toggleButton.setBounds(bounds.removeFromLeft(50).reduced(5));
            break;
        case ControlType::ComboBox:
            comboBox.setBounds(bounds.reduced(2));
            break;
        case ControlType::Slider:
            slider.setBounds(bounds);
            break;
        }
    }

private:
    enum class ControlType
    {
        Slider,
        ToggleButton,
        ComboBox
    };

    AudioPluginAudioProcessor& processor;
    int index;
    ControlType controlType = ControlType::Slider;

    juce::Slider slider;
    juce::ToggleButton toggleButton;
    juce::ComboBox comboBox;
    juce::Label nameLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboBoxAttachment;
};

//==============================================================================
class AudioPluginAudioProcessorEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void loadJSFXFile();
    void unloadJSFXFile();
    void rebuildParameterSliders();

    AudioPluginAudioProcessor& processorRef;

    juce::TextButton loadButton{"Load JSFX"};
    juce::TextButton unloadButton{"Unload"};
    juce::TextButton uiButton{"UI"};
    juce::TextButton editButton{"Editor"};
    juce::TextButton ioMatrixButton{"I/O Matrix"};
    juce::Slider wetSlider;
    juce::Label wetLabel;
    juce::Viewport viewport;
    juce::Component parameterContainer;
    juce::Label statusLabel;

    juce::OwnedArray<ParameterSlider> parameterSliders;
    std::unique_ptr<PersistentFileChooser> fileChooser;

    // All platforms: Use LICE framebuffer rendering for cross-platform consistency
    std::unique_ptr<JsfxLiceComponent> jsfxLiceRenderer;

    std::unique_ptr<IOMatrixWindow> ioMatrixWindow;
    std::unique_ptr<JsfxEditorWindow> jsfxEditorWindow;

    void destroyJsfxUI();
    void toggleIOMatrix();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
