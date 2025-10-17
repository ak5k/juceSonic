#include "PluginEditor.h"

#include "IOMatrixComponent.h"
#include "JsfxLogger.h"
#include "PersistentFileChooser.h"
#include "PluginProcessor.h"
#include "ReaperPresetConverter.h"

#include <memory>

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , processorRef(p)
{
    // Initialize state tree for persistent state management
    setStateTree(processorRef.getAPVTS().state);

    addAndMakeVisible(loadButton);
    loadButton.onClick = [this]() { loadJSFXFile(); };

    addAndMakeVisible(unloadButton);
    unloadButton.onClick = [this]() { unloadJSFXFile(); };

    addAndMakeVisible(editButton);
    editButton.setEnabled(false); // Disabled until JSFX is loaded
    editButton.setClickingTogglesState(true);
    editButton.onClick = [this]()
    {
        auto* instance = processorRef.getSXInstancePtr();
        if (!instance)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "No JSFX Loaded",
                "Please load a JSFX file first before editing."
            );
            return;
        }

        // Create editor window if needed
        if (!jsfxEditorWindow)
            jsfxEditorWindow = std::make_unique<JsfxEditorWindow>();

        // Toggle: if already open, close it; otherwise open it
        if (jsfxEditorWindow->isOpen())
        {
            jsfxEditorWindow->close();
            editButton.setButtonText("Editor");
            editButton.setToggleState(false, juce::dontSendNotification);
        }
        else
        {
            jsfxEditorWindow->open(instance, this);
            editButton.setButtonText("Close Editor");
            editButton.setToggleState(true, juce::dontSendNotification);
        }
    };

    addAndMakeVisible(uiButton);
    uiButton.onClick = [this]()
    {
        // Toggle between JUCE controls and LICE-rendered JSFX UI
        if (jsfxLiceRenderer && jsfxLiceRenderer->isVisible())
        {
            // Currently showing LICE - switch to JUCE
            // Step 1: Store current LICE size (per-JSFX via PersistentState)
            setStateProperty("liceUIWidth", getWidth());
            setStateProperty("liceUIHeight", getHeight());

            // Step 2: Hide LICE, show JUCE
            jsfxLiceRenderer->setVisible(false);
            viewport.setVisible(true);
            uiButton.setButtonText("UI");

            // Step 3: Set JUCE resize limits
            setResizeLimits(700, 170, 700, 1080);

            // Step 4: Read and apply JUCE size (per-JSFX via PersistentState)
            int juceWidth = getStateProperty("juceControlsWidth", 700);
            int juceHeight = getStateProperty("juceControlsHeight", -1);

            if (juceHeight < 0)
            {
                // No saved JUCE size - calculate from parameters
                int numParams = processorRef.getNumActiveParameters();
                int sliderHeight = 60;
                juceHeight = 40 + 30 + (numParams * sliderHeight) + 20;
                juceHeight = juce::jlimit(170, 800, juceHeight);
            }

            DBG("Switching to JUCE view: " << juceWidth << "x" << juceHeight);
            setSize(juceWidth, juceHeight);
            resized();
            return;
        }

        // Currently showing JUCE - switch to LICE
        // Step 1: Store current JUCE size (per-JSFX via PersistentState)
        setStateProperty("juceControlsWidth", getWidth());
        setStateProperty("juceControlsHeight", getHeight());

        // Step 2: Create LICE renderer if needed
        if (!jsfxLiceRenderer)
        {
            auto* sx = processorRef.getSXInstancePtr();
            if (!sx)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "No JSFX Loaded",
                    "Please load a JSFX file first before opening the UI."
                );
                return;
            }

            jsfxLiceRenderer = std::make_unique<JsfxLiceComponent>(sx, processorRef);
            addAndMakeVisible(*jsfxLiceRenderer);
        }

        // Step 3: Read LICE size (per-JSFX via PersistentState)
        int liceWidth = getStateProperty("liceUIWidth", -1);
        int liceHeight = getStateProperty("liceUIHeight", -1);

        if (liceWidth < 0 || liceHeight < 0)
        {
            // No saved LICE size - get recommended size from JSFX
            auto recommended = jsfxLiceRenderer->getRecommendedBounds();
            int jsfxWidth = recommended.getWidth();
            int jsfxHeight = recommended.getHeight();

            // Calculate total size including buttons and status
            int totalHeight = 40 + 30 + jsfxHeight;
            int totalWidth = juce::jmax(700, jsfxWidth);
            totalHeight = juce::jlimit(170, 1080, totalHeight);
            totalWidth = juce::jlimit(600, 1920, totalWidth);

            liceWidth = totalWidth;
            liceHeight = totalHeight;
        }

        // Step 4: Hide JUCE, show LICE
        viewport.setVisible(false);
        jsfxLiceRenderer->setVisible(true);
        uiButton.setButtonText("Params");

        // Step 5: Set LICE resize limits and apply size
        setResizeLimits(400, 300, 1920, 1080);
        DBG("Switching to LICE view: " << liceWidth << "x" << liceHeight);
        setSize(liceWidth, liceHeight);
        resized();
    };

    addAndMakeVisible(ioMatrixButton);
    ioMatrixButton.onClick = [this]() { toggleIOMatrix(); };

    // Preset library browser
    addAndMakeVisible(libraryBrowser);
    libraryBrowser.attachToValueTree(processorRef.getAPVTS().state, "PresetLibrary");
    libraryBrowser.setConverter(std::make_unique<ReaperPresetConverter>());
    libraryBrowser.setLabelText("Presets:");                 // Set label text
    libraryBrowser.setPlaceholderText("(No preset loaded)"); // Set placeholder
    libraryBrowser.setItemSelectedCallback(
        [this](const juce::String& category, const juce::String& label, const juce::String& itemData)
        { onPresetSelected(category, label, itemData); }
    );

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
    statusLabel.setFont(juce::FontOptions(18.0f).withStyle("Bold"));
    statusLabel.setText("No JSFX loaded", juce::dontSendNotification);

    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&parameterContainer, false);

    // Make the editor resizable with constraints
    // Min height: 40px buttons + 30px status + 100px content = 170px
    setResizable(true, true);
    setResizeLimits(600, 170, 1920, 1080);

    // Restore editor state from processor (per-JSFX via PersistentState)
    bool showingJsfxUI = getStateProperty("editorShowingJsfxUI", true);

    // Store size to be restored - defer actual resize to avoid DAW override
    if (showingJsfxUI)
    {
        // Was showing LICE UI - restore LICE size (per-JSFX)
        restoredWidth = getStateProperty("liceUIWidth", 700);
        restoredHeight = getStateProperty("liceUIHeight", 500);
    }
    else
    {
        // Was showing JUCE controls - restore JUCE size (per-JSFX)
        restoredWidth = getStateProperty("juceControlsWidth", 700);
        restoredHeight = getStateProperty("juceControlsHeight", 500);
    }

    // Set initial size (may be overridden by DAW)
    setSize(restoredWidth, restoredHeight);

    // Flag that we need to restore size in timer callback
    needsSizeRestoration = true;

    rebuildParameterSliders();

    // If JSFX is already loaded at startup, enable the Edit button and schedule preset list update
    if (processorRef.getSXInstancePtr() != nullptr)
    {
        editButton.setEnabled(true);
        needsPresetListUpdate = true; // Defer preset list update to timer
    }

    // If we should be showing JSFX UI and there's a JSFX loaded, show it
    if (showingJsfxUI && processorRef.getSXInstancePtr() != nullptr)
    {
        // Trigger the UI button to show JSFX (will be created via timer)
        uiButton.triggerClick();
    }

    // 30fps = ~33ms interval (also pumps SWELL message loop on Linux)
    startTimer(33);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    // Save editor state to processor (per-JSFX via PersistentState)
    setStateProperty("editorShowingJsfxUI", jsfxLiceRenderer && jsfxLiceRenderer->isVisible());

    // Save current size to the appropriate property based on which view is showing (per-JSFX)
    if (jsfxLiceRenderer && jsfxLiceRenderer->isVisible())
    {
        setStateProperty("liceUIWidth", getWidth());
        setStateProperty("liceUIHeight", getHeight());
    }
    else
    {
        setStateProperty("juceControlsWidth", getWidth());
        setStateProperty("juceControlsHeight", getHeight());
    }

    // Stop timer first to prevent callbacks during destruction
    stopTimer();

    // Ensure native JSFX UI is torn down before editor destruction
    destroyJsfxUI(); // This now properly destroys the native window too
}

void AudioPluginAudioProcessorEditor::destroyJsfxUI()
{
    // Close the editor window if it's open
    if (jsfxEditorWindow)
        jsfxEditorWindow->close();

    // Cleanup LICE renderer
    if (jsfxLiceRenderer)
    {
        DBG("PluginEditor: Destroying JSFX LICE renderer");
        jsfxLiceRenderer->setVisible(false);
        jsfxLiceRenderer.reset();
    }
}

//==============================================================================
// JSFX Lifecycle Management (Internal Constructor/Destructor Pattern)

void AudioPluginAudioProcessorEditor::saveJsfxState()
{
    // Save current editor state before unloading JSFX (internal "destructor")
    // This ensures state is preserved when reloading the same JSFX

    if (jsfxLiceRenderer)
    {
        setStateProperty("editorShowingJsfxUI", jsfxLiceRenderer->isVisible());

        if (jsfxLiceRenderer->isVisible())
        {
            setStateProperty("liceUIWidth", getWidth());
            setStateProperty("liceUIHeight", getHeight());
            DBG("Saved LICE UI state: " << getWidth() << "x" << getHeight());
        }
        else
        {
            setStateProperty("juceControlsWidth", getWidth());
            setStateProperty("juceControlsHeight", getHeight());
            DBG("Saved JUCE controls state: " << getWidth() << "x" << getHeight());
        }
    }
    else if (viewport.isVisible())
    {
        // No LICE renderer - must be showing JUCE controls
        setStateProperty("editorShowingJsfxUI", false);
        setStateProperty("juceControlsWidth", getWidth());
        setStateProperty("juceControlsHeight", getHeight());
        DBG("Saved JUCE controls state (no LICE): " << getWidth() << "x" << getHeight());
    }
}

void AudioPluginAudioProcessorEditor::restoreJsfxState()
{
    // Restore editor state after loading JSFX (internal "constructor")
    // Uses per-JSFX saved state if available, otherwise uses defaults

    // Check if this JSFX has saved state (PersistentState is automatically per-JSFX)
    bool hasSavedState = getStateProperty("editorShowingJsfxUI", juce::var()).isVoid() == false;
    bool showingJsfxUI = getStateProperty("editorShowingJsfxUI", true);

    DBG("Restoring JSFX state - hasSavedState="
        << (hasSavedState ? "true" : "false")
        << ", showingJsfxUI="
        << (showingJsfxUI ? "true" : "false"));

    auto* sx = processorRef.getSXInstancePtr();
    if (sx && sx->gfx_hasCode())
    {
        // JSFX has @gfx section - create LICE renderer
        jsfxLiceRenderer = std::make_unique<JsfxLiceComponent>(sx, processorRef);
        addAndMakeVisible(*jsfxLiceRenderer);
        viewport.setVisible(false);
        uiButton.setButtonText("Params");
        uiButton.setEnabled(true);
        editButton.setEnabled(true);

        // Calculate default size from JSFX's @gfx dimensions
        auto bounds = jsfxLiceRenderer->getRecommendedBounds();
        int defaultHeight = 40 + 30 + bounds.getHeight();
        int defaultWidth = juce::jmax(700, bounds.getWidth());
        defaultHeight = juce::jlimit(300, 1080, defaultHeight);
        defaultWidth = juce::jlimit(400, 1920, defaultWidth);

        // Make fully resizable
        setResizeLimits(400, 300, 1920, 1080);

        // Restore state based on what was showing before
        if (hasSavedState && showingJsfxUI)
        {
            // Was showing LICE UI - restore saved LICE size
            restoredWidth = getStateProperty("liceUIWidth", defaultWidth);
            restoredHeight = getStateProperty("liceUIHeight", defaultHeight);
            DBG("Restoring LICE UI size: " << restoredWidth << "x" << restoredHeight);
        }
        else if (hasSavedState && !showingJsfxUI)
        {
            // Was showing JUCE controls - restore JUCE size and switch view
            int numParams = processorRef.getNumActiveParameters();
            int sliderHeight = 60;
            int defaultJuceHeight = 40 + 30 + (numParams * sliderHeight) + 20;
            defaultJuceHeight = juce::jlimit(170, 800, defaultJuceHeight);

            restoredWidth = getStateProperty("juceControlsWidth", 700);
            restoredHeight = getStateProperty("juceControlsHeight", defaultJuceHeight);

            // Switch to JUCE view
            jsfxLiceRenderer->setVisible(false);
            viewport.setVisible(true);
            uiButton.setButtonText("UI");
            setResizeLimits(700, 170, 700, 1080);

            DBG("Restoring JUCE controls view: " << restoredWidth << "x" << restoredHeight);
        }
        else
        {
            // No saved state - use defaults (show LICE UI)
            restoredWidth = defaultWidth;
            restoredHeight = defaultHeight;
            DBG("No saved state - using LICE defaults: " << restoredWidth << "x" << restoredHeight);
        }

        // Defer resize to next timer tick for DAW compatibility
        needsSizeRestoration = true;
    }
    else
    {
        // No @gfx section - show JUCE parameter controls
        viewport.setVisible(true);
        uiButton.setButtonText("UI");
        uiButton.setEnabled(false);
        editButton.setEnabled(true);

        // Make resizable only vertically for JUCE controls
        setResizeLimits(700, 170, 700, 1080);

        // Calculate default size to fit parameters
        int numParams = processorRef.getNumActiveParameters();
        int sliderHeight = 60;
        int defaultHeight = 40 + 30 + (numParams * sliderHeight) + 20;
        defaultHeight = juce::jlimit(170, 800, defaultHeight);

        // Use saved state if available, otherwise use defaults
        if (hasSavedState)
        {
            restoredWidth = getStateProperty("juceControlsWidth", 700);
            restoredHeight = getStateProperty("juceControlsHeight", defaultHeight);
            DBG("Restoring JUCE controls (no GFX): " << restoredWidth << "x" << restoredHeight);
        }
        else
        {
            restoredWidth = 700;
            restoredHeight = defaultHeight;
            DBG("No saved state - using JUCE defaults: " << restoredWidth << "x" << restoredHeight);
        }

        // Defer resize to next timer tick for DAW compatibility
        needsSizeRestoration = true;
    }
}

//==============================================================================

void AudioPluginAudioProcessorEditor::timerCallback()
{
    // Apply deferred size restoration (once, after DAW has initialized the window)
    if (needsSizeRestoration)
    {
        setSize(restoredWidth, restoredHeight);
        needsSizeRestoration = false;
    }

    // Apply deferred preset list update (after JSFX has been loaded and preset scanning is complete)
    if (needsPresetListUpdate)
    {
        updatePresetList();
        needsPresetListUpdate = false;
    }

#if defined(__linux__) || defined(SWELL_TARGET_GDK)
    // On Linux with GDK, pump the SWELL message loop to process window events, redraws, and timers
    // This is needed for JSFX UI to work properly
    // Note: Not needed on macOS - Cocoa handles the message loop automatically
    extern void SWELL_RunMessageLoop();
    SWELL_RunMessageLoop();
#endif

    juce::String statusText = "No JSFX loaded";
    if (!processorRef.getCurrentJSFXName().isEmpty())
        statusText = processorRef.getCurrentJSFXName();
    statusLabel.setText(statusText, juce::dontSendNotification);

    // Update wet slider if it changed elsewhere
    if (std::abs(wetSlider.getValue() - processorRef.getWetAmount()) > 0.001)
        wetSlider.setValue(processorRef.getWetAmount(), juce::dontSendNotification);

    // Sync Edit button text with editor window state
    if (jsfxEditorWindow)
    {
        if (jsfxEditorWindow->isOpen())
        {
            if (editButton.getButtonText() != "Close Editor")
            {
                editButton.setButtonText("Close Editor");
                editButton.setToggleState(true, juce::dontSendNotification);
            }
        }
        else if (editButton.getButtonText() != "Editor")
        {
            editButton.setButtonText("Editor");
            editButton.setToggleState(false, juce::dontSendNotification);
        }
    }

    // Sync I/O Matrix button text with window state
    if (ioMatrixWindow)
    {
        if (ioMatrixWindow->isVisible())
        {
            if (ioMatrixButton.getButtonText() != "Close I/O Matrix")
                ioMatrixButton.setButtonText("Close I/O Matrix");
        }
        else
        {
            if (ioMatrixButton.getButtonText() != "I/O Matrix")
                ioMatrixButton.setButtonText("I/O Matrix");
        }
    }
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
    editButton.setBounds(buttonArea.removeFromLeft(100));
    buttonArea.removeFromLeft(5);
    uiButton.setBounds(buttonArea.removeFromLeft(100));
    buttonArea.removeFromLeft(5);
    ioMatrixButton.setBounds(buttonArea.removeFromLeft(100));

    buttonArea.removeFromLeft(10);
    libraryBrowser.setBounds(buttonArea.removeFromLeft(265)); // Label + ComboBox

    buttonArea.removeFromLeft(10);
    wetLabel.setBounds(buttonArea.removeFromLeft(50));
    buttonArea.removeFromLeft(5);
    wetSlider.setBounds(buttonArea.removeFromLeft(150));

    statusLabel.setBounds(bounds.removeFromTop(30));

    // Give remaining space to components - visibility controls which shows
    viewport.setBounds(bounds);

    if (jsfxLiceRenderer)
        jsfxLiceRenderer->setBounds(bounds);

    // Save current size to appropriate property based on which view is showing
    // This ensures sizes are persisted when host calls getStateInformation() (per-JSFX via PersistentState)
    if (jsfxLiceRenderer && jsfxLiceRenderer->isVisible())
    {
        setStateProperty("liceUIWidth", getWidth());
        setStateProperty("liceUIHeight", getHeight());
    }
    else
    {
        setStateProperty("juceControlsWidth", getWidth());
        setStateProperty("juceControlsHeight", getHeight());
    }
}

void AudioPluginAudioProcessorEditor::loadJSFXFile()
{
    // Use PersistentFileChooser for consistent directory management
    // Only show files without extension or with .jsfx extension
    auto fileChooser =
        std::make_unique<PersistentFileChooser>("lastJsfxDirectory", "Select a JSFX file to load...", "*.jsfx;*.");

    fileChooser->launchAsync(
        [this](const juce::File& file)
        {
            if (file != juce::File{})
            {
                JsfxLogger::info("Editor", "User selected JSFX file: " + file.getFullPathName());

                // Save current JSFX state before unloading (internal "destructor")
                saveJsfxState();

                // Ensure any native window is closed before reloading a new JSFX
                destroyJsfxUI();

                // Suspend audio processing during JSFX loading to prevent crashes
                processorRef.suspendProcessing(true);

                bool loadSuccess = processorRef.loadJSFX(file);

                // Resume audio processing after loading
                processorRef.suspendProcessing(false);

                if (loadSuccess)
                {
                    rebuildParameterSliders();

                    // Defer preset list update to allow preset scanning to complete
                    needsPresetListUpdate = true;

                    JsfxLogger::info("Editor", "JSFX loaded successfully");

                    // Restore JSFX state after loading (internal "constructor")
                    restoreJsfxState();
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
                // Save current JSFX state before unloading (internal "destructor")
                saveJsfxState();

                // Ensure any native window is closed before unloading JSFX
                destroyJsfxUI();

                // Suspend audio processing during JSFX unloading to prevent crashes
                processorRef.suspendProcessing(true);

                processorRef.unloadJSFX();

                // Resume audio processing after unloading
                processorRef.suspendProcessing(false);

                rebuildParameterSliders();

                // Clear preset libraries
                libraryBrowser.clearLibrary();
                libraryBrowser.updateItemList();

                // Reset UI state - show parameters, update buttons
                viewport.setVisible(true);
                uiButton.setButtonText("UI");
                uiButton.setEnabled(false);
                editButton.setEnabled(false); // Disable Edit button when no JSFX loaded
                resized();
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
    // Use a fixed width that matches the editor width (700 pixels)
    // This prevents the scrollbar from appearing due to width issues
    const int fixedWidth = 680; // Leave some margin
    parameterContainer.setSize(fixedWidth, totalHeight);

    int y = 0;
    for (auto* slider : parameterSliders)
    {
        slider->setBounds(0, y, fixedWidth, 35);
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

void AudioPluginAudioProcessorEditor::toggleIOMatrix()
{
    if (ioMatrixWindow && ioMatrixWindow->isVisible())
    {
        ioMatrixWindow->setVisible(false);
        ioMatrixButton.setButtonText("I/O Matrix");
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

        // Load routing state from APVTS (per-JSFX via PersistentState)
        juce::String routingState = getStateProperty<juce::String>("ioMatrixRouting", juce::String());
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
            setStateProperty("ioMatrixRouting", state);

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
    ioMatrixButton.setButtonText("Close I/O Matrix");
}

void AudioPluginAudioProcessorEditor::updatePresetList()
{
    // Scan JSFX preset directories for .rpl files
    juce::StringArray presetPaths;

    // Add default JSFX Effects path if it exists
    auto jsfxPath = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("REAPER")
                        .getChildFile("Effects");

    DBG("updatePresetList: Checking JSFX path: " << jsfxPath.getFullPathName());
    DBG("updatePresetList: Path exists: " << (jsfxPath.exists() ? "YES" : "NO"));

    if (jsfxPath.exists())
    {
        presetPaths.add(jsfxPath.getFullPathName());

        // List .rpl files found
        auto rplFiles = jsfxPath.findChildFiles(juce::File::findFiles, true, "*.rpl");
        DBG("updatePresetList: Found " << rplFiles.size() << " .rpl files");
    }

    // TODO: Add any additional preset paths from plugin settings/configuration

    DBG("updatePresetList: Total preset paths to scan: " << presetPaths.size());

    // Load presets from directories (converter handles file filtering)
    int filesLoaded = libraryBrowser.loadLibrary(
        presetPaths, // Directories to scan
        true,        // Recursive scan
        true         // Clear existing
    );

    DBG("updatePresetList: Loaded " << filesLoaded << " preset files");
    DBG("updatePresetList: Total children in tree: " << libraryBrowser.getLibraryTree().getNumChildren());

    // Update the browser UI to reflect loaded libraries
    libraryBrowser.updateItemList();
}

void AudioPluginAudioProcessorEditor::onPresetSelected(
    const juce::String& category,
    const juce::String& label,
    const juce::String& itemData
)
{
    DBG("LibraryBrowser item selected:");
    DBG("  Category: " << category);
    DBG("  Label: " << label);
    DBG("  Data length: " << itemData.length() << " chars");
    DBG("  Data preview: " << itemData.substring(0, 50) << "...");

    // The itemData is base64 encoded JSFX state
    // Pass it directly to the processor to decode and apply
    bool success = processorRef.loadPresetFromData(itemData);

    if (success)
        DBG("Preset loaded successfully: " << label);
    else
        DBG("Failed to load preset: " << label);
}

//==============================================================================
