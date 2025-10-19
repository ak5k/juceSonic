#include "PluginEditor.h"

#include "AboutWindow.h"
#include "IOMatrixComponent.h"
#include "PersistentFileChooser.h"
#include "PluginProcessor.h"
#include "PresetWindow.h"
#include "RepositoryWindow.h"

#include <jsfx.h>
#include <memory>

extern jsfxAPI JesusonicAPI;

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , processorRef(p)
    , presetWindow(p)
{
    setLookAndFeel(&sharedLookAndFeel->lf);

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

        setSize(liceWidth, liceHeight);
        resized();
    };

    addAndMakeVisible(ioMatrixButton);
    ioMatrixButton.onClick = [this]() { toggleIOMatrix(); };

    // Preset management menu
    addAndMakeVisible(presetManagementMenu);
    setupPresetManagementMenu();

    // Preset browser (embedded, minimal UI)
    addAndMakeVisible(presetWindow);
    presetWindow.setShowManagementButtons(false);           // Hide import/export/etc buttons
    presetWindow.getTreeView().setShowMetadataLabel(false); // Hide metadata label
    presetWindow.getTreeView().setAutoHideTreeWithoutResults(
        true
    );                           // Show hint line when no search, expand to show results
    presetWindow.toFront(false); // Ensure it's on top of LICE component

    // PresetWindow now handles preset selection internally via its own callback

    // Handle tree expansion for adaptive sizing
    presetWindow.getTreeView().onTreeExpansionChanged = [this](bool isExpanded)
    {
        // Trigger layout update when tree height changes
        resized();
        // Ensure preset window stays on top when expanded
        if (isExpanded)
            presetWindow.toFront(false);
    };

    // Update preset label when preset is selected
    presetWindow.getTreeView().onCommand = [this](const juce::Array<juce::TreeViewItem*>& items)
    {
        if (items.size() > 0 && items[0] != nullptr)
        {
            auto* searchableItem = dynamic_cast<SearchableTreeItem*>(items[0]);
            if (searchableItem != nullptr)
            {
                auto presetName = searchableItem->getName();
                presetLabel.setText(presetName, juce::dontSendNotification);
                // Save preset name for persistence (per-JSFX)
                setStateProperty("lastPresetName", presetName);
            }
        }
    };

    // Wet amount slider
    addAndMakeVisible(wetLabel);
    wetLabel.setText("Dry/Wet", juce::dontSendNotification);
    wetLabel.setJustificationType(juce::Justification::centredRight);
    wetLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    addAndMakeVisible(wetSlider);
    wetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    wetSlider.setRange(0.0, 1.0, 0.01);
    wetSlider.setValue(processorRef.getWetAmount(), juce::dontSendNotification);
    wetSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); // No text box
    wetSlider.onValueChange = [this]() { processorRef.setWetAmount(wetSlider.getValue()); };

    addAndMakeVisible(titleLabel);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::FontOptions(18.0f).withStyle("Bold"));
    titleLabel.setText("No JSFX loaded", juce::dontSendNotification);

    addAndMakeVisible(presetLabel);
    presetLabel.setJustificationType(juce::Justification::centred);
    presetLabel.setFont(juce::FontOptions(16.0f)); // Almost as big as title (18pt)
    presetLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    presetLabel.setText("", juce::dontSendNotification);

    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&parameterContainer, false);
    viewport.setScrollBarThickness(16); // Make scrollbars thicker (default is 12)

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

    // Restore last preset name (per-JSFX)
    juce::String lastPresetName = getStateProperty("lastPresetName", juce::String());
    if (lastPresetName.isNotEmpty())
        presetLabel.setText(lastPresetName, juce::dontSendNotification);

    rebuildParameterSliders();

    // Listen to APVTS state for preset updates
    processorRef.getAPVTS().state.addListener(this);

    // If JSFX is already loaded at startup, enable the Edit button and load presets
    if (processorRef.getSXInstancePtr() != nullptr)
    {
        editButton.setEnabled(true);
        // Load preset list for the currently loaded JSFX
        updatePresetList();
    }

    // If we should be showing JSFX UI and there's a JSFX loaded, show it
    if (showingJsfxUI && processorRef.getSXInstancePtr() != nullptr)
    {
        // Trigger the UI button to show JSFX (will be created via timer)
        uiButton.triggerClick();
    }

    // 30fps = ~33ms interval (also pumps SWELL message loop on Linux)
    startTimer(33);

    // Enable keyboard focus for F11 fullscreen toggle
    setWantsKeyboardFocus(true);

    // Check for updates monthly
    checkForUpdatesIfNeeded();
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

    // Stop listening to APVTS state
    processorRef.getAPVTS().state.removeListener(this);

    // Stop timer first to prevent callbacks during destruction
    stopTimer();

    // Ensure native JSFX UI is torn down before editor destruction
    destroyJsfxUI(); // This now properly destroys the native window too

    // Remove LookAndFeel before destruction
    setLookAndFeel(nullptr);
}

void AudioPluginAudioProcessorEditor::destroyJsfxUI()
{
    if (jsfxEditorWindow)
        jsfxEditorWindow->close();

    jsfxLiceFullscreenWindow.reset();

    if (jsfxLiceRenderer)
    {
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
        }
        else
        {
            setStateProperty("juceControlsWidth", getWidth());
            setStateProperty("juceControlsHeight", getHeight());
        }
    }
    else if (viewport.isVisible())
    {
        // No LICE renderer - must be showing JUCE controls
        setStateProperty("editorShowingJsfxUI", false);
        setStateProperty("juceControlsWidth", getWidth());
        setStateProperty("juceControlsHeight", getHeight());
    }
}

void AudioPluginAudioProcessorEditor::restoreJsfxState()
{
    // Restore editor state after loading JSFX (internal "constructor")
    // Uses per-JSFX saved state if available, otherwise uses defaults

    // Check if this JSFX has saved state (PersistentState is automatically per-JSFX)
    bool hasSavedState = getStateProperty("editorShowingJsfxUI", juce::var()).isVoid() == false;
    bool showingJsfxUI = getStateProperty("editorShowingJsfxUI", true);

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
        int defaultWidth = bounds.getWidth();
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
        }
        else
        {
            // No saved state - use defaults (show LICE UI)
            restoredWidth = defaultWidth;
            restoredHeight = defaultHeight;
        }

        // Defer resize to next timer tick for DAW compatibility
        needsSizeRestoration = true;
    }
    else
    {
        // No @gfx section - show JUCE parameter controls only
        viewport.setVisible(true);
        uiButton.setButtonText("UI");
        uiButton.setEnabled(false); // No LICE UI to toggle to
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
        }
        else
        {
            restoredWidth = 700;
            restoredHeight = defaultHeight;
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
    titleLabel.setText(statusText, juce::dontSendNotification);

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
    auto originalBounds = bounds; // Keep original bounds for tree overlay calculation

    // Reserve space for JSFX title and preset label at the very top
    auto titleArea = bounds.removeFromTop(50); // Increased from 30 to 50 for both labels
    titleArea.reduce(5, 2);

    // Only allocate button area space if button bar is visible
    juce::Rectangle<int> buttonArea;
    if (buttonBarVisible)
    {
        // Button area to fit search field (25px) + spacing (5px) + tree line (24px) + extra spacing = 64px total
        buttonArea = bounds.removeFromTop(64);

        // Apply vertical margin
        buttonArea.removeFromTop(5);
        buttonArea.removeFromBottom(5);

        // Apply left margin (more distance from left edge) and right margin
        buttonArea.removeFromLeft(10); // Increased from 5 to 10 for more distance from left edge
        buttonArea.removeFromRight(5);

        int totalWidth = buttonArea.getWidth();
        int spacing = 5;

        // Fixed minimum sizes that must fit
        int buttonWidth = 60;        // Minimum for each button
        int presetMenuWidth = 40;    // Width for preset management menu (just arrow)
        int wetLabelWidth = 60;      // Enough for "Dry/Wet" text
        int wetSliderMinWidth = 100; // Minimum usable slider width
        int wetSliderMaxWidth = 200; // Maximum to prevent it getting too large
        int libraryMinWidth = 150;   // Minimum for library browser

        // Calculate minimum required width
        int minRequired = (buttonWidth * 5)
                        + (spacing * 4)
                        + spacing
                        + presetMenuWidth
                        + (spacing * 2)
                        + libraryMinWidth
                        + (spacing * 2)
                        + wetLabelWidth
                        + spacing
                        + wetSliderMinWidth;

        // If we have extra space, distribute it proportionally
        int extraSpace = juce::jmax(0, totalWidth - minRequired);

        // Distribute extra space: 70% to library, 30% to wet slider (capped at max)
        int libraryWidth = libraryMinWidth + (int)(extraSpace * 0.7f);

        // If wet slider hit its max, give remaining space to library
        if (wetSliderMinWidth + (int)(extraSpace * 0.3f) > wetSliderMaxWidth)
        {
            int wetExcess = (wetSliderMinWidth + (int)(extraSpace * 0.3f)) - wetSliderMaxWidth;
            libraryWidth += wetExcess;
        }

        // Layout buttons and set visibility
        loadButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
        loadButton.setVisible(true);
        buttonArea.removeFromLeft(spacing);
        unloadButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
        unloadButton.setVisible(true);
        buttonArea.removeFromLeft(spacing);
        editButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
        editButton.setVisible(true);
        buttonArea.removeFromLeft(spacing);
        uiButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
        uiButton.setVisible(true);
        buttonArea.removeFromLeft(spacing);
        ioMatrixButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
        ioMatrixButton.setVisible(true);

        buttonArea.removeFromLeft(spacing);
        presetManagementMenu.setBounds(buttonArea.removeFromLeft(presetMenuWidth));
        presetManagementMenu.setVisible(true);

        buttonArea.removeFromLeft(spacing * 2);

        // Calculate preset window bounds - search field and tree (title is separate now)
        auto presetWindowArea = buttonArea.removeFromLeft(libraryWidth);

        // Always use adaptive height - tree resizes based on content/search
        auto& treeView = presetWindow.getTreeView();
        int neededHeight = treeView.getNeededHeight();
        presetWindowArea.setHeight(neededHeight);
        presetWindow.setBounds(presetWindowArea);
        presetWindow.setVisible(true); // Ensure visible when button bar is shown

        buttonArea.removeFromLeft(spacing * 2);
        wetLabel.setBounds(buttonArea.removeFromLeft(wetLabelWidth));
        wetLabel.setVisible(true);
        buttonArea.removeFromLeft(spacing);

        // Give remaining space to wet slider (should match wetSliderWidth calculated above)
        wetSlider.setBounds(buttonArea);
        wetSlider.setVisible(true);
    }
    else
    {
        // Hide all button bar components when not visible
        loadButton.setVisible(false);
        unloadButton.setVisible(false);
        editButton.setVisible(false);
        uiButton.setVisible(false);
        ioMatrixButton.setVisible(false);
        presetManagementMenu.setVisible(false);
        presetWindow.setVisible(false);
        wetLabel.setVisible(false);
        wetSlider.setVisible(false);
    }

    // Position JSFX title and preset labels in the reserved title area with spacing
    auto titleHeight = titleArea.getHeight() / 2; // Split area in half
    titleLabel.setBounds(titleArea.removeFromTop(titleHeight));
    titleArea.removeFromTop(8);       // Add gap between title and preset label
    presetLabel.setBounds(titleArea); // Remaining space for preset label

    // Give remaining space to components - visibility controls which shows
    viewport.setBounds(bounds);

    // Resize parameter container to match viewport width dynamically
    if (viewport.isVisible() && parameterContainer.isVisible())
    {
        // Use actual viewport width minus scrollbar, no artificial minimum
        int viewportInnerWidth = viewport.getWidth() - viewport.getScrollBarThickness();
        int containerWidth = juce::jmax(200, viewportInnerWidth); // Only prevent extreme collapse
        int containerHeight = parameterSliders.size() * 40;
        parameterContainer.setSize(containerWidth, containerHeight);

        // Resize all parameter sliders to match container width
        int y = 0;
        for (auto* slider : parameterSliders)
        {
            slider->setBounds(0, y, containerWidth, 35);
            y += 40;
        }
    }

    if (jsfxLiceRenderer)
        jsfxLiceRenderer->setBounds(bounds);

    // Ensure preset window is always on top (after all other layout is done)
    presetWindow.toFront(false);

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

bool AudioPluginAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // Global keyboard shortcuts:
    // - Shift + / : Focus search field
    // - Ctrl + F  : Focus search field
    // - F         : Toggle button bar visibility
    // - F11       : Toggle fullscreen mode

    // Shift + / or Ctrl + F - Focus search field
    if ((key.getKeyCode() == '/' && key.getModifiers().isShiftDown())
        || (key.getKeyCode() == 'F' && key.getModifiers().isCtrlDown()))
    {
        if (buttonBarVisible && presetWindow.isVisible())
        {
            presetWindow.getTreeView().moveFocusToSearchField();
            return true;
        }
    }

    // F key - Toggle button bar visibility
    if (key.getTextCharacter() == 'f' || key.getTextCharacter() == 'F')
    {
        // Only toggle if no modifiers are pressed (to avoid conflict with Ctrl+F)
        if (!key.getModifiers().isAnyModifierKeyDown())
        {
            buttonBarVisible = !buttonBarVisible;
            resized(); // Trigger layout update
            return true;
        }
    }

    // F11 - Toggle LICE fullscreen
    if (key == juce::KeyPress::F11Key)
    {
        if (jsfxLiceRenderer && jsfxLiceRenderer->isVisible())
        {
            toggleLiceFullscreen();
            return true;
        }
        else
        {
            // If no LICE renderer is visible, still toggle fullscreen for the main window
            toggleLiceFullscreen();
            return true;
        }
    }

    return Component::keyPressed(key);
}

void AudioPluginAudioProcessorEditor::toggleLiceFullscreen()
{
    if (!jsfxLiceRenderer)
        return;

    if (jsfxLiceFullscreenWindow && jsfxLiceFullscreenWindow->isVisible())
    {
        // Exit fullscreen and kiosk mode
        juce::Desktop::getInstance().setKioskModeComponent(nullptr);
        jsfxLiceFullscreenWindow.reset();

        // Re-add LICE component to main editor
        addAndMakeVisible(jsfxLiceRenderer.get());

        resized();
        grabKeyboardFocus();
    }
    else
    {
        // Enter fullscreen - show LICE component in fullscreen window
        jsfxLiceFullscreenWindow = std::make_unique<JsfxLiceFullscreenWindow>();
        jsfxLiceFullscreenWindow->onWindowClosed = [this]() { toggleLiceFullscreen(); };

        jsfxLiceFullscreenWindow->showWithComponent(jsfxLiceRenderer.get());

        // Use Desktop::setKioskModeComponent for true kiosk mode
        juce::Desktop::getInstance().setKioskModeComponent(jsfxLiceFullscreenWindow.get());
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

                    // Update preset list after JSFX loads
                    updatePresetList();

                    // Always clear state when manually loading a JSFX (new session)
                    // State restoration only preserves state when DAW reopens the plugin
                    clearCurrentJsfxState();

                    // Restore JSFX state after loading (internal "constructor")
                    // Since we just cleared state, this will use defaults
                    restoreJsfxState();
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

                // Clear preset browser (PresetLoader will handle clearing APVTS)
                presetWindow.refreshPresetList();

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

    // Calculate initial size - will be properly sized in resized()
    int containerWidth =
        viewport.getWidth() > 0 ? juce::jmax(200, viewport.getWidth() - viewport.getScrollBarThickness()) : 600;
    int totalHeight = numParams * 40;

    parameterContainer.setSize(containerWidth, totalHeight);

    int y = 0;
    for (auto* slider : parameterSliders)
    {
        slider->setBounds(0, y, containerWidth, 35);
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

        // Create window (LookAndFeel is set via SharedResourcePointer)
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
    // Trigger preset refresh - PresetWindow will load from APVTS and refresh tree
    presetWindow.refreshPresetList();
}

//==============================================================================
// Preset Management

void AudioPluginAudioProcessorEditor::setupPresetManagementMenu()
{
    presetManagementMenu.setTextWhenNothingSelected(""); // No text, just arrow
    presetManagementMenu.setTextWhenNoChoicesAvailable("");

    // Add menu items
    presetManagementMenu.addItem("Presets...", 1);
    presetManagementMenu.addItem("Repositories...", 2);
    presetManagementMenu.addSeparator();
    presetManagementMenu.addItem("About...", 3);

    // Handle selection
    presetManagementMenu.onChange = [this]()
    {
        int selectedId = presetManagementMenu.getSelectedId();
        if (selectedId > 0)
        {
            handlePresetManagementSelection(selectedId);
            // Reset to "nothing selected" after action
            presetManagementMenu.setSelectedId(0, juce::dontSendNotification);
        }
    };
}

void AudioPluginAudioProcessorEditor::handlePresetManagementSelection(int selectedId)
{
    switch (selectedId)
    {
    case 1: // Presets...
        openPresetManager();
        break;

    case 2: // Repositories...
        showRepositoryBrowser();
        break;

    case 3: // About...
        showAboutWindow();
        break;

    default:
        break;
    }
}

void AudioPluginAudioProcessorEditor::showAboutWindow()
{
    // Create About window as a top-level window
    // It will delete itself when closed
    new AboutWindow();
}

void AudioPluginAudioProcessorEditor::checkForUpdatesIfNeeded()
{
    // Check if user has opted out of update checks
    bool shouldCheck = getGlobalProperty("shouldCheckForUpdates", true);
    if (!shouldCheck)
        return;

    // Get last check timestamp (stored as int64 milliseconds since epoch)
    auto lastCheck = getGlobalProperty("lastUpdateCheckTime", (juce::int64)0);
    auto now = juce::Time::currentTimeMillis();

    // Check once per month (30 days = 2,592,000,000 milliseconds)
    const juce::int64 monthInMs = 30LL * 24 * 60 * 60 * 1000;

    if (now - lastCheck < monthInMs)
        return; // Already checked recently

    // Update last check time
    setGlobalProperty("lastUpdateCheckTime", now);

    // Get repository URL from CMake configuration
#ifdef JUCESONIC_REPO_URL
    juce::String repoUrl = JUCESONIC_REPO_URL;
#else
    juce::String repoUrl = "https://github.com/ak5k/jucesonic";
#endif

    // Start async version check
    if (!versionChecker)
        versionChecker = std::make_unique<VersionChecker>();

    versionChecker->onUpdateCheckComplete =
        [this](bool updateAvailable, const juce::String& latestVersion, const juce::String& downloadUrl)
    {
        if (updateAvailable)
        {
            showUpdateNotification(latestVersion, downloadUrl);
        }
        else
        {
        }
    };

    versionChecker->checkForUpdates(JucePlugin_VersionString, repoUrl);
}

void AudioPluginAudioProcessorEditor::showUpdateNotification(
    const juce::String& latestVersion,
    const juce::String& downloadUrl
)
{
    // Show a non-modal alert with a clickable link
    juce::String message = "A new version of juceSonic is available!\n\n"
                           "Current version: " + juce::String(JucePlugin_VersionString) + "\n"
                           "Latest version: " + latestVersion + "\n\n"
                           "Would you like to download it now?";

    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::InfoIcon)
                       .withTitle("Update Available")
                       .withMessage(message)
                       .withButton("Download")
                       .withButton("Later")
                       .withButton("Don't Ask Again");

    juce::AlertWindow::showAsync(
        options,
        [downloadUrl, this](int result)
        {
            if (result == 1) // Download button
                juce::URL(downloadUrl).launchInDefaultBrowser();
            else if (result == 3) // Don't Ask Again
                setGlobalProperty("shouldCheckForUpdates", false);
        }
    );
}

void AudioPluginAudioProcessorEditor::openPresetManager()
{
    auto* windowContent = new PresetWindow(processorRef);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(windowContent);
    options.dialogTitle = "Preset Manager";
    options.resizable = true;
    options.useNativeTitleBar = true;

    auto* window = options.launchAsync();
    if (window != nullptr)
        window->centreWithSize(700, 600);
}

void AudioPluginAudioProcessorEditor::showRepositoryBrowser()
{
    // RepositoryWindow now owns its own RepositoryManager
    auto* repoWindow = new RepositoryWindow(processorRef);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(repoWindow);
    options.dialogTitle = "Repository Manager";
    options.resizable = true;
    options.useNativeTitleBar = true;

    auto* window = options.launchAsync();
    if (window != nullptr)
        window->centreWithSize(800, 600);
}

//==============================================================================
// ValueTree::Listener implementation

void AudioPluginAudioProcessorEditor::valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child)
{
    // Check if the "presets" node was added or modified
    if (parent == processorRef.getAPVTS().state && child.getType() == juce::Identifier("presets"))
    {
        // Update UI on message thread (we're already on it since this is a ValueTree listener)
        updatePresetList();
    }
}

void AudioPluginAudioProcessorEditor::valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree& child, int)
{
    // Check if the "presets" node was removed
    if (parent == processorRef.getAPVTS().state && child.getType() == juce::Identifier("presets"))

        updatePresetList();
}

//==============================================================================
