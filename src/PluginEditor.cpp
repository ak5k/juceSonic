#include "PluginEditor.h"

#include "IOMatrixComponent.h"
#include "JsfxLogger.h"
#include "JsfxNativeWindow.h"
#include "PersistentFileChooser.h"
#include "PluginProcessor.h"

#include <memory>

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , processorRef(p)
{
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this]() { loadJSFXFile(); };

    addAndMakeVisible(unloadButton);
    unloadButton.onClick = [this]() { unloadJSFXFile(); };

    addAndMakeVisible(uiButton);
    uiButton.onClick = [this]() { openJSFXUI(); };

    addAndMakeVisible(ioMatrixButton);
    ioMatrixButton.onClick = [this]() { toggleIOMatrix(); };

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
    // 30fps = ~33ms interval (also pumps SWELL message loop on Linux)
    startTimer(33);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    // Stop timer first to prevent callbacks during destruction
    stopTimer();

    // Ensure native JSFX UI is torn down before editor destruction
    destroyJsfxUI();
}

void AudioPluginAudioProcessorEditor::destroyJsfxUI()
{
    if (jsfxWindow)
        jsfxWindow.reset();
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
#if defined(__linux__) || defined(SWELL_TARGET_GDK)
    // On Linux with GDK, pump the SWELL message loop to process window events, redraws, and timers
    // This is needed for JSFX UI to work properly
    // Note: Not needed on macOS - Cocoa handles the message loop automatically
    extern void SWELL_RunMessageLoop();
    SWELL_RunMessageLoop();
#endif

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
    buttonArea.removeFromLeft(5);
    uiButton.setBounds(buttonArea.removeFromLeft(100));
    buttonArea.removeFromLeft(5);
    ioMatrixButton.setBounds(buttonArea.removeFromLeft(100));
    buttonArea.removeFromLeft(10);
    wetLabel.setBounds(buttonArea.removeFromLeft(50));
    buttonArea.removeFromLeft(5);
    wetSlider.setBounds(buttonArea.removeFromLeft(150));

    statusLabel.setBounds(bounds.removeFromTop(30));
    // Split remaining space: top half for JSFX UI (if any), bottom half for parameters
    viewport.setBounds(bounds);
}

void AudioPluginAudioProcessorEditor::loadJSFXFile()
{
    // Use PersistentFileChooser for consistent directory management
    auto fileChooser =
        std::make_unique<PersistentFileChooser>("lastJsfxDirectory", "Select a JSFX file to load...", "*");

    fileChooser->launchAsync(
        [this](const juce::File& file)
        {
            if (file != juce::File{})
            {
                JsfxLogger::info("Editor", "User selected JSFX file: " + file.getFullPathName());

                // Ensure any native window is closed before reloading a new JSFX
                destroyJsfxUI();

                if (processorRef.loadJSFX(file))
                {
                    rebuildParameterSliders();
                    JsfxLogger::info("Editor", "JSFX loaded successfully");
                }
                else
                {
                    JsfxLogger::error("Editor", "Failed to load JSFX file: " + file.getFullPathName());
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Error",
                        "Failed to load JSFX file: " + file.getFullPathName()
                    );
                }
            }
        }
    );

    // Keep the file chooser alive by storing it as a member (it will auto-delete when done)
    this->fileChooser = std::move(fileChooser);
}

void AudioPluginAudioProcessorEditor::unloadJSFXFile()
{
    // Show confirmation dialog
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Unload JSFX")
                       .withMessage("Are you sure you want to unload the current JSFX effect?")
                       .withButton("Yes")
                       .withButton("No")
                       .withAssociatedComponent(this);

    juce::AlertWindow::showAsync(
        options,
        [this](int result)
        {
            if (result == 1) // Yes button
            {
                // Ensure any native window is closed before unloading JSFX
                destroyJsfxUI();

                processorRef.unloadJSFX();
                rebuildParameterSliders();
            }
        }
    );
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

    // Calculate optimal window size based on number of parameters
    const int headerHeight = 40;    // Button area
    const int statusHeight = 30;    // Status label
    const int parameterHeight = 40; // Height per parameter
    const int minHeight = 200;

    // Get screen dimensions
    auto displays = juce::Desktop::getInstance().getDisplays();
    auto mainDisplay = displays.getPrimaryDisplay();
    if (mainDisplay != nullptr)
    {
        auto screenArea = mainDisplay->userArea;
        int maxWindowHeight = (screenArea.getHeight() * 2) / 3; // 2/3 of screen height

        // Calculate desired height
        int desiredHeight = headerHeight + statusHeight + (numParams * parameterHeight);

        // Clamp to reasonable bounds
        int newHeight = juce::jmax(minHeight, juce::jmin(desiredHeight, maxWindowHeight));

        setSize(700, newHeight);
    }
}

void AudioPluginAudioProcessorEditor::openJSFXUI()
{
    auto* sxInstance = processorRef.getSXInstancePtr();
    if (!sxInstance)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "No JSFX Loaded",
            "Please load a JSFX file first before opening the UI."
        );
        return;
    }

    // Open JSFX native UI in its own window (cross-platform via SWELL)
    // Close any existing window
    destroyJsfxUI();
    auto* sx = processorRef.getSXInstancePtr();
    jsfxWindow = std::make_unique<JsfxNativeWindow>(sx, processorRef.getCurrentJSFXName() + " - UI", processorRef);

    // Connect I/O button to show I/O Matrix
    jsfxWindow->onIOButtonClicked = [this]() { toggleIOMatrix(); };

    jsfxWindow->setAlwaysOnTop(false);
    jsfxWindow->setVisible(true);
}

void AudioPluginAudioProcessorEditor::toggleIOMatrix()
{
    if (ioMatrixWindow && ioMatrixWindow->isVisible())
    {
        ioMatrixWindow->setVisible(false);
        return;
    }

    if (!ioMatrixWindow)
    {
        // Get bus layout information
        auto bus = processorRef.getBusesLayout();
        int numJuceInputs = bus.getMainInputChannels();
        int numJuceSidechains = 0;
        if (processorRef.getBus(true, 1) != nullptr)
            numJuceSidechains = bus.getNumChannels(true, 1);
        int numJuceOutputs = bus.getMainOutputChannels();

        // Get JSFX channel counts
        int numJsfxInputs = numJuceInputs;
        int numJsfxSidechains = numJuceSidechains;
        int numJsfxOutputs = numJuceOutputs;

        // Create I/O Matrix component
        auto* ioMatrix = new IOMatrixComponent(
            numJuceInputs,
            numJuceSidechains,
            numJuceOutputs,
            numJsfxInputs,
            numJsfxSidechains,
            numJsfxOutputs
        );

        // Load routing state from APVTS
        auto routingState = processorRef.getAPVTS().state.getProperty("ioMatrixRouting", "").toString();
        if (routingState.isNotEmpty())
        {
            ioMatrix->setRoutingState(routingState);

            // Also update processor immediately with loaded state
            RoutingConfig config;
            config.numJuceInputs = numJuceInputs;
            config.numJuceSidechains = numJuceSidechains;
            config.numJuceOutputs = numJuceOutputs;
            config.numJsfxInputs = numJsfxInputs;
            config.numJsfxSidechains = numJsfxSidechains;
            config.numJsfxOutputs = numJsfxOutputs;

            for (int r = 0; r < numJuceInputs; ++r)
                for (int c = 0; c < numJsfxInputs; ++c)
                    config.inputRouting[r][c] = ioMatrix->getInputRouting(r, c);

            for (int r = 0; r < numJuceSidechains; ++r)
                for (int c = 0; c < numJsfxSidechains; ++c)
                    config.sidechainRouting[r][c] = ioMatrix->getSidechainRouting(r, c);

            for (int r = 0; r < numJsfxOutputs; ++r)
                for (int c = 0; c < numJuceOutputs; ++c)
                    config.outputRouting[r][c] = ioMatrix->getOutputRouting(r, c);

            processorRef.updateRoutingConfig(config);
        }

        // Save routing changes to APVTS and update processor
        ioMatrix->onRoutingChanged = [this,
                                      ioMatrix,
                                      numJuceInputs,
                                      numJuceSidechains,
                                      numJuceOutputs,
                                      numJsfxInputs,
                                      numJsfxSidechains,
                                      numJsfxOutputs]()
        {
            auto state = ioMatrix->getRoutingState();
            processorRef.getAPVTS().state.setProperty("ioMatrixRouting", state, nullptr);

            // Build RoutingConfig from IOMatrix state and send to processor
            RoutingConfig config;
            config.numJuceInputs = numJuceInputs;
            config.numJuceSidechains = numJuceSidechains;
            config.numJuceOutputs = numJuceOutputs;
            config.numJsfxInputs = numJsfxInputs;
            config.numJsfxSidechains = numJsfxSidechains;
            config.numJsfxOutputs = numJsfxOutputs;

            // Copy routing from IOMatrix to config
            for (int r = 0; r < numJuceInputs; ++r)
                for (int c = 0; c < numJsfxInputs; ++c)
                    config.inputRouting[r][c] = ioMatrix->getInputRouting(r, c);

            for (int r = 0; r < numJuceSidechains; ++r)
                for (int c = 0; c < numJsfxSidechains; ++c)
                    config.sidechainRouting[r][c] = ioMatrix->getSidechainRouting(r, c);

            for (int r = 0; r < numJsfxOutputs; ++r)
                for (int c = 0; c < numJuceOutputs; ++c)
                    config.outputRouting[r][c] = ioMatrix->getOutputRouting(r, c);

            // Update processor with new routing (lock-free)
            processorRef.updateRoutingConfig(config);
        };

        // Get ideal bounds from matrix component
        auto idealBounds = ioMatrix->getIdealBounds();

        // Create window with custom close handling
        ioMatrixWindow = std::make_unique<IOMatrixWindow>();
        ioMatrixWindow->setContentOwned(ioMatrix, true);
        ioMatrixWindow->centreWithSize(idealBounds.getWidth(), idealBounds.getHeight());
    }

    ioMatrixWindow->setVisible(true);
    ioMatrixWindow->toFront(true);
}

// Windows-specific embedded UI helpers removed; using separate window instead
