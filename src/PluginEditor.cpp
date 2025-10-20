#include "PluginEditor.h"

#include "AboutWindow.h"
#include "IOMatrixComponent.h"
#include "PersistentFileChooser.h"
#include "PluginProcessor.h"
#include "PresetWindow.h"
#include "JsfxPluginWindow.h"

#include <jsfx.h>
#include <memory>

extern jsfxAPI JesusonicAPI;

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , processorRef(p)
    , jsfxPluginWindow(p)
    , presetWindow(p)
{
    setLookAndFeel(&sharedLookAndFeel->lf);

    // Initialize state tree for persistent state management
    setStateTree(processorRef.getAPVTS().state);

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
            jsfxLiceRenderer = std::make_unique<JsfxLiceComponent>();
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

            // Calculate total editor size to fit JSFX UI plus chrome:
            // - Title area: 50px, Button area: 92px, JSFX height, Bottom margin: 16px
            int totalHeight = 50 + 92 + jsfxHeight + PluginConstants::LiceComponentExtraHeightPixels;
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

    addAndMakeVisible(aboutButton);
    aboutButton.onClick = [this]() { showAboutWindow(); };

    // JSFX Plugin browser (embedded with management buttons)
    addAndMakeVisible(jsfxPluginWindow);
    jsfxPluginWindow.setShowManagementButtons(true);                    // Show management buttons
    jsfxPluginWindow.setStatusLabelVisible(false);                      // Hide status label in embedded mode
    jsfxPluginWindow.getTreeView().setShowMetadataLabel(false);         // Hide metadata label
    jsfxPluginWindow.getTreeView().setAutoHideTreeWithoutResults(true); // Show hint line, expand on search
    jsfxPluginWindow.toFront(false);                                    // Ensure it's on top of LICE component

    // Set up callback for when a plugin is selected from the embedded browser
    jsfxPluginWindow.onPluginSelected = [this](const juce::String& pluginPath)
    {
        juce::ignoreUnused(pluginPath);

        // Call common code path to update UI
        onJsfxLoaded();
    };

    // Handle JSFX plugin tree expansion for adaptive sizing
    jsfxPluginWindow.getTreeView().onTreeExpansionChanged = [this](bool isExpanded)
    {
        if (buttonBarVisible && jsfxPluginWindow.isVisible())
        {
            auto currentBounds = jsfxPluginWindow.getBounds();
            int treeViewHeight = jsfxPluginWindow.getTreeView().getNeededHeight();

            // Add WindowWithButtonRow button row (status label is hidden in embedded mode)
            const int buttonRowHeight = 30;
            const int buttonRowSpacing = 8;
            int totalHeight = buttonRowHeight + buttonRowSpacing + treeViewHeight;

            // Constrain to available vertical space in parent (leave some padding at bottom)
            int maxAvailableHeight = getHeight() - currentBounds.getY() - 10; // 10px bottom padding
            totalHeight = juce::jmin(totalHeight, maxAvailableHeight);

            // Only update height, preserve x, y, and width
            jsfxPluginWindow
                .setBounds(currentBounds.getX(), currentBounds.getY(), currentBounds.getWidth(), totalHeight);
        }

        // Ensure plugin window stays on top when expanded (only if not in overlay mode)
        if (isExpanded && !jsfxPluginWindow.getTreeView().getTreeView().isOverlayMode)
            jsfxPluginWindow.toFront(false);
    };

    // Preset browser (embedded with management buttons)
    addAndMakeVisible(presetWindow);
    presetWindow.setShowManagementButtons(true);            // Show import/export/etc buttons
    presetWindow.setStatusLabelVisible(false);              // Hide "Loaded n presets" status label in embedded mode
    presetWindow.getTreeView().setShowMetadataLabel(false); // Hide metadata label
    presetWindow.getTreeView().setAutoHideTreeWithoutResults(
        true
    );                           // Show hint line when no search, expand to show results
    presetWindow.toFront(false); // Ensure it's on top of LICE component

    // PresetWindow now handles preset selection internally via its own callback

    // Handle tree expansion for adaptive sizing
    presetWindow.getTreeView().onTreeExpansionChanged = [this](bool isExpanded)
    {
        // Directly update only the preset window height instead of triggering full resized()
        // This avoids feedback loops and glitches with scrollbars
        if (buttonBarVisible && presetWindow.isVisible())
        {
            auto currentBounds = presetWindow.getBounds();
            int treeViewHeight = presetWindow.getTreeView().getNeededHeight();

            // Add WindowWithButtonRow button row (status label is hidden in embedded mode)
            const int buttonRowHeight = 30;
            const int buttonRowSpacing = 8;
            int totalHeight = buttonRowHeight + buttonRowSpacing + treeViewHeight;

            // Constrain to available vertical space in parent (leave some padding at bottom)
            int maxAvailableHeight = getHeight() - currentBounds.getY() - 10; // 10px bottom padding
            totalHeight = juce::jmin(totalHeight, maxAvailableHeight);

            // Only update height, preserve x, y, and width
            presetWindow.setBounds(currentBounds.getX(), currentBounds.getY(), currentBounds.getWidth(), totalHeight);
        }

        // Ensure preset window stays on top when expanded (only if not in overlay mode)
        if (isExpanded && !presetWindow.getTreeView().getTreeView().isOverlayMode)
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

    // Restore last preset name (per-JSFX)
    juce::String lastPresetName = getStateProperty("lastPresetName", juce::String());
    if (lastPresetName.isNotEmpty())
        presetLabel.setText(lastPresetName, juce::dontSendNotification);

    rebuildParameterSliders();

    // Listen to preset cache updates
    processorRef.getPresetCache().onCacheUpdated = [this]() { updatePresetList(); };

    // If JSFX is already loaded at startup (from setStateInformation), restore UI state
    if (processorRef.getSXInstancePtr() != nullptr)
    {
        editButton.setEnabled(true);
        // Load preset list for the currently loaded JSFX
        updatePresetList();

        // Update title label to show loaded JSFX name
        updateTitleLabel();

        // Defer JSFX UI state restoration to next event loop cycle (after window is fully initialized)
        juce::MessageManager::callAsync(
            [this]()
            {
                restoreJsfxUiState();
                setSize(restoredWidth, restoredHeight);
            }
        );
    }

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

    // Clear preset cache callback
    processorRef.getPresetCache().onCacheUpdated = nullptr;

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

void AudioPluginAudioProcessorEditor::restoreJsfxUiState()
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
        jsfxLiceRenderer = std::make_unique<JsfxLiceComponent>();
        addAndMakeVisible(*jsfxLiceRenderer);
        viewport.setVisible(false);
        uiButton.setButtonText("Params");
        uiButton.setEnabled(true);
        editButton.setEnabled(true);

        // Calculate default size from JSFX's @gfx dimensions
        auto bounds = jsfxLiceRenderer->getRecommendedBounds();

        // Calculate editor height to fit JSFX UI plus chrome:
        // - Title area: 50px (title + preset label)
        // - Button area: 92px (when visible)
        // - JSFX bounds: bounds.getHeight()
        // - Bottom margin: 16px
        int defaultHeight = 50 + 92 + bounds.getHeight() + PluginConstants::LiceComponentExtraHeightPixels;
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
    }
}

//==============================================================================

void AudioPluginAudioProcessorEditor::onJsfxLoaded()
{
    // Common code path after JSFX is loaded (manually or from saved state)
    // This handles all UI updates and state restoration

    // Save current JSFX state before making changes (if switching from another JSFX)
    saveJsfxState();

    // Ensure any native window is closed
    destroyJsfxUI();

    // Rebuild UI for the newly loaded JSFX
    rebuildParameterSliders();

    // Update preset list
    updatePresetList();

    // Update title label to show new JSFX name
    updateTitleLabel();

    // Clear state for new JSFX (fresh start)
    clearCurrentJsfxState();

    // Defer JSFX UI state restoration to next event loop cycle
    juce::MessageManager::callAsync(
        [this]()
        {
            restoreJsfxUiState();
            setSize(restoredWidth, restoredHeight);
        }
    );
}

//==============================================================================

void AudioPluginAudioProcessorEditor::updateTitleLabel()
{
    juce::String statusText = "No JSFX loaded";
    if (!processorRef.getCurrentJSFXName().isEmpty())
        statusText = processorRef.getCurrentJSFXName();
    titleLabel.setText(statusText, juce::dontSendNotification);
}

void AudioPluginAudioProcessorEditor::updateEditorButtonState()
{
    if (jsfxEditorWindow)
    {
        if (jsfxEditorWindow->isOpen())
        {
            editButton.setButtonText("Close Editor");
            editButton.setToggleState(true, juce::dontSendNotification);
        }
        else
        {
            editButton.setButtonText("Editor");
            editButton.setToggleState(false, juce::dontSendNotification);
        }
    }
}

void AudioPluginAudioProcessorEditor::updateIOMatrixButtonState()
{
    if (ioMatrixWindow)
    {
        if (ioMatrixWindow->isVisible())
            ioMatrixButton.setButtonText("Close I/O Matrix");
        else
            ioMatrixButton.setButtonText("I/O Matrix");
    }
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void AudioPluginAudioProcessorEditor::resized()
{
    // Collapse tree views when editor is resized
    jsfxPluginWindow.getTreeView().collapseTree();
    presetWindow.getTreeView().collapseTree();

    auto bounds = getLocalBounds();
    auto originalBounds = bounds; // Keep original bounds for tree overlay calculation

    // Reserve space for JSFX title and preset label at the very top
    auto titleArea = bounds.removeFromTop(50); // Increased from 30 to 50 for both labels
    titleArea.reduce(5, 2);

    // Only allocate button area space if button bar is visible
    juce::Rectangle<int> buttonArea;
    if (buttonBarVisible)
    {
        // Button area to fit:
        // - Button row with top margin (34px: 4px margin + 30px buttons)
        // - Search field (25px) + spacing (5px)
        // - Tree hint line (24px) + spacing (4px)
        // Total: ~92px (when tree expands further, it will overlay the content area below)
        buttonArea = bounds.removeFromTop(92);

        // Apply horizontal margins only
        buttonArea.removeFromLeft(10); // Distance from left edge
        buttonArea.removeFromRight(5);

        int totalWidth = buttonArea.getWidth();
        int spacing = 5;

        // Fixed minimum sizes that must fit
        int buttonWidth = 60;         // Minimum for each button
        int pluginBrowserWidth = 150; // Width for JSFX plugin browser
        int presetBrowserWidth = 150; // Width for preset browser

        // Calculate minimum required width (5 buttons: Unload, Editor, UI, I/O Matrix, About)
        int minRequired =
            pluginBrowserWidth + spacing + presetBrowserWidth + spacing + (buttonWidth * 5) + (spacing * 4);

        // If we have extra space, distribute it equally to plugin and preset browsers
        int extraSpace = juce::jmax(0, totalWidth - minRequired);
        int extraPerBrowser = extraSpace / 2;
        pluginBrowserWidth += extraPerBrowser;
        presetBrowserWidth += extraPerBrowser + (extraSpace % 2); // Add remainder to preset browser

        // Store the original Y position and full height for window positioning
        int originalY = buttonArea.getY();
        int originalHeight = buttonArea.getHeight();

        // Create button row area with 4px top offset to align with window button rows
        auto buttonRowArea = buttonArea.removeFromTop(30);             // 30px button height
        buttonRowArea = buttonRowArea.withY(buttonRowArea.getY() + 4); // Offset by 4px to match window margins

        // Layout JSFX plugin browser on the left
        auto jsfxPluginWindowArea =
            juce::Rectangle<int>(buttonRowArea.getX(), originalY, pluginBrowserWidth, originalHeight);
        jsfxPluginWindow.setBounds(jsfxPluginWindowArea);
        jsfxPluginWindow.setVisible(true);
        // Only bring to front if tree is not in overlay mode
        if (!jsfxPluginWindow.getTreeView().getTreeView().isOverlayMode)
            jsfxPluginWindow.toFront(false);

        // Move past the plugin browser
        buttonRowArea.removeFromLeft(pluginBrowserWidth);
        buttonRowArea.removeFromLeft(spacing);

        // Layout preset browser next
        auto presetWindowArea =
            juce::Rectangle<int>(buttonRowArea.getX(), originalY, presetBrowserWidth, originalHeight);
        presetWindow.setBounds(presetWindowArea);
        presetWindow.setVisible(true);
        // Only bring to front if tree is not in overlay mode
        if (!presetWindow.getTreeView().getTreeView().isOverlayMode)
            presetWindow.toFront(false);

        // Move past the preset browser
        buttonRowArea.removeFromLeft(presetBrowserWidth);
        buttonRowArea.removeFromLeft(spacing);

        // Layout main buttons on the right (full 30px height)
        unloadButton.setBounds(buttonRowArea.removeFromLeft(buttonWidth));
        unloadButton.setVisible(true);
        buttonRowArea.removeFromLeft(spacing);
        editButton.setBounds(buttonRowArea.removeFromLeft(buttonWidth));
        editButton.setVisible(true);
        buttonRowArea.removeFromLeft(spacing);
        uiButton.setBounds(buttonRowArea.removeFromLeft(buttonWidth));
        uiButton.setVisible(true);
        buttonRowArea.removeFromLeft(spacing);
        ioMatrixButton.setBounds(buttonRowArea.removeFromLeft(buttonWidth));
        ioMatrixButton.setVisible(true);
        buttonRowArea.removeFromLeft(spacing);
        aboutButton.setBounds(buttonRowArea.removeFromLeft(buttonWidth));
        aboutButton.setVisible(true);
    }
    else
    {
        // Hide all button bar components when not visible
        jsfxPluginWindow.setVisible(false);
        unloadButton.setVisible(false);
        editButton.setVisible(false);
        uiButton.setVisible(false);
        ioMatrixButton.setVisible(false);
        aboutButton.setVisible(false);
        presetWindow.setVisible(false);
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
    // - Shift + / : Focus search field (legacy - cycles through search fields)
    // - Ctrl + F  : Cycle through search fields
    // - F         : Toggle button bar visibility
    // - F11       : Toggle fullscreen mode
    // - ESC       : Collapse all expanded trees
    // - W/A/S/D   : Preset navigation (when WASD mode enabled)

    // ESC key - Collapse all expanded trees
    if (key == juce::KeyPress::escapeKey)
    {
        SearchableTreeView::collapseAllExpandedTrees();
        return true;
    }

    // Shift + / or Ctrl + F - Cycle through search fields
    if ((key.getKeyCode() == '/' && key.getModifiers().isShiftDown())
        || (key.getKeyCode() == 'F' && key.getModifiers().isCtrlDown()))
    {
        // Use global cycling system to move to next SearchableTreeView instance
        SearchableTreeView::focusNextSearchField();
        return true;
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

    // WASD preset navigation (when enabled)
    if (presetWindow.isWASDModeEnabled() && !key.getModifiers().isAnyModifierKeyDown())
    {
        auto keyChar = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

        if (keyChar == 'a')
        {
            // A = Previous preset
            presetWindow.navigateToPreviousPreset();
            return true;
        }
        else if (keyChar == 'd')
        {
            // D = Next preset
            presetWindow.navigateToNextPreset();
            return true;
        }
        else if (keyChar == 'w')
        {
            // W = Jump back 10
            presetWindow.navigatePresetJump(-10);
            return true;
        }
        else if (keyChar == 's')
        {
            // S = Jump forward 10
            presetWindow.navigatePresetJump(10);
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
                    // Call common code path to update UI
                    onJsfxLoaded();
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

void AudioPluginAudioProcessorEditor::openJsfxPluginBrowser()
{
    auto* windowContent = new JsfxPluginWindow(processorRef);

    // Set up callback to handle UI updates when a plugin is loaded
    windowContent->onPluginSelected = [this](const juce::String& pluginPath)
    {
        juce::ignoreUnused(pluginPath);

        // Call common code path to update UI
        onJsfxLoaded();
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(windowContent);
    options.dialogTitle = "JSFX Plugins";
    options.resizable = true;
    options.useNativeTitleBar = true;

    auto* window = options.launchAsync();
    if (window != nullptr)
        window->centreWithSize(700, 600);
}

//==============================================================================
