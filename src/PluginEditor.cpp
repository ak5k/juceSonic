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

    // UI button removed - parameters and LICE are now always shown together (when @gfx exists)
    // The button is kept in code but hidden from UI
    addChildComponent(uiButton); // addChildComponent instead of addAndMakeVisible - hidden by default
    uiButton.setEnabled(false);  // Disabled - no longer used for toggling

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

    // Set initial default size - will be properly sized by prepareJsfxUi() if JSFX is loaded
    restoredWidth = 700;
    restoredHeight = 500;
    setSize(restoredWidth, restoredHeight);

    // Restore last preset name (per-JSFX)
    juce::String lastPresetName = getStateProperty("lastPresetName", juce::String());
    if (lastPresetName.isNotEmpty())
        presetLabel.setText(lastPresetName, juce::dontSendNotification);

    rebuildParameterSliders();

    // Listen to preset cache updates
    processorRef.getPresetCache().onCacheUpdated = [this]() { updatePresetList(); };

    // If JSFX is already loaded at startup (from setStateInformation), prepare UI
    if (processorRef.getSXInstancePtr() != nullptr)
    {
        editButton.setEnabled(true);

        // Update the JSFX file path in state tree for per-JSFX scoping
        processorRef.getAPVTS().state.setProperty("jsfxFilePath", processorRef.getCurrentJSFXPath(), nullptr);

        // Load preset list for the currently loaded JSFX
        updatePresetList();

        // Update title label to show loaded JSFX name
        updateTitleLabel();

        // Prepare JSFX UI immediately (calculates proper size based on GFX and saved state)
        prepareJsfxUi();

        // Apply the prepared size
        setSize(restoredWidth, restoredHeight);
    }

    // Enable keyboard focus for F11 fullscreen toggle
    setWantsKeyboardFocus(true);

    // Check for updates monthly
    checkForUpdatesIfNeeded();
}

void AudioPluginAudioProcessorEditor::saveEditorState()
{
    // Called by processor's getStateInformation() before serializing state.
    // This ensures the current window size is saved at the right time in the shutdown sequence.
    setStateProperty("editorWidth", getWidth());
    setStateProperty("editorHeight", getHeight());
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    // Clear preset cache callback
    processorRef.getPresetCache().onCacheUpdated = nullptr;

    // Ensure native JSFX UI is torn down before editor destruction
    destroyJsfxUI();

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
// JSFX UI Preparation

void AudioPluginAudioProcessorEditor::prepareJsfxUi(bool restoreSavedSize)
{
    // Prepare UI after loading JSFX (internal "constructor")
    // Assumes sx is valid - calculates UI size based on GFX and parameters
    // Uses per-JSFX saved state if restoreSavedSize=true, otherwise uses defaults

    auto* sx = processorRef.getSXInstancePtr();
    jassert(sx != nullptr); // Should always be valid when called

    bool hasGfx = sx->gfx_hasCode();

    // Count visible parameters
    int numVisibleParams = 0;
    int numTotalParams = processorRef.getNumActiveParameters();
    for (int i = 0; i < numTotalParams; ++i)
        if (processorRef.isJSFXParameterVisible(i))
            numVisibleParams++;

    // Show parameter viewport only if there are visible parameters
    viewport.setVisible(numVisibleParams > 0);
    editButton.setEnabled(true);

    // Calculate parameter area height
    int parameterAreaHeight = numVisibleParams * PluginConstants::ParameterSliderHeight;

    if (hasGfx)
    {
        // JSFX has @gfx section - show parameters above LICE renderer
        // Create or refresh LICE renderer
        if (!jsfxLiceRenderer)
        {
            jsfxLiceRenderer = std::make_unique<JsfxLiceComponent>();
            addAndMakeVisible(*jsfxLiceRenderer);
        }
        jsfxLiceRenderer->setVisible(true);

        // Calculate default size from JSFX's @gfx dimensions
        auto gfxBounds = jsfxLiceRenderer->getRecommendedBounds();
        int gfxWidth = gfxBounds.getWidth();
        int gfxHeight = gfxBounds.getHeight();

        // Ensure we have sensible GFX dimensions
        // If getRecommendedBounds() returns fallback (400x300), use it
        // Otherwise ensure minimum of 100x100 to catch any edge cases
        if (gfxWidth <= 0)
            gfxWidth = 400;
        if (gfxHeight <= 0)
            gfxHeight = 300;
        gfxWidth = juce::jmax(100, gfxWidth);
        gfxHeight = juce::jmax(100, gfxHeight);

        // Calculate editor size to fit all components:
        // - Title area: 50px (title + preset label)
        // - Button area: 92px (when visible)
        // - Parameters: parameterAreaHeight
        // - JSFX GFX: gfxHeight
        int defaultHeight = 50 + 92 + parameterAreaHeight + gfxHeight + PluginConstants::LiceComponentExtraHeightPixels;

        // Width: if we have visible parameters, need at least 700px for controls
        //        otherwise, use GFX width (with minimum of 400px)
        int defaultWidth = (numVisibleParams > 0) ? juce::jmax(700, gfxWidth) : juce::jmax(400, gfxWidth);

        // Get screen dimensions for maximum size limits
        auto displays = juce::Desktop::getInstance().getDisplays();
        auto* primaryDisplay = displays.getPrimaryDisplay();
        auto screenArea = primaryDisplay ? primaryDisplay->userArea : juce::Rectangle<int>(0, 0, 1920, 1080);
        int maxScreenWidth = screenArea.getWidth();
        int maxScreenHeight = screenArea.getHeight();

        // Calculate minimum height and resize limits based on whether we have visible parameters
        int minHeight, maxHeight;
        int minWidth, maxWidth;

        if (numVisibleParams > 0)
        {
            // With visible parameters: need space for controls
            minHeight = 300;
            maxHeight = maxScreenHeight;
            minWidth = 700;
            // Allow width to expand if JSFX requested a wider GFX width (but don't exceed screen)
            maxWidth = juce::jlimit(minWidth, maxScreenWidth, juce::jmax(700, gfxWidth));
        }
        else
        {
            // GFX only: freely resizable to allow scaling
            minHeight = 300; // Title + buttons + some GFX space
            maxHeight = maxScreenHeight;
            minWidth = 400;
            maxWidth = maxScreenWidth; // Full width range
        }

        defaultHeight = juce::jlimit(minHeight, maxHeight, defaultHeight);
        defaultWidth = juce::jlimit(minWidth, maxWidth, defaultWidth);

        // Set resize limits
        setResizeLimits(minWidth, minHeight, maxWidth, maxHeight);

        // Restore saved size if requested and available, otherwise use defaults
        int savedWidth = -1;
        int savedHeight = -1;

        if (restoreSavedSize)
        {
            // Try scoped key first, then fall back to global key for migration from old versions
            savedWidth = getStateProperty("editorWidth", -1);
            if (savedWidth == -1)
                savedWidth = getGlobalProperty("editorWidth", -1);

            savedHeight = getStateProperty("editorHeight", -1);
            if (savedHeight == -1)
                savedHeight = getGlobalProperty("editorHeight", -1);
        }

        // Use saved size if available, otherwise use defaults
        // LICE component will scale to fit whatever size we give it
        if (savedWidth > 0)
            restoredWidth = juce::jlimit(minWidth, maxWidth, savedWidth);
        else
            restoredWidth = juce::jlimit(minWidth, maxWidth, defaultWidth);

        if (savedHeight > 0)
            restoredHeight = juce::jlimit(minHeight, maxHeight, savedHeight);
        else
            restoredHeight = juce::jlimit(minHeight, maxHeight, defaultHeight);
    }
    else
    {
        // No @gfx section - show JUCE parameter controls only
        // Destroy LICE renderer if it exists
        if (jsfxLiceRenderer)
        {
            jsfxLiceRenderer->setVisible(false);
            jsfxLiceRenderer.reset();
        }

        // Get screen dimensions for maximum size limits
        auto displays = juce::Desktop::getInstance().getDisplays();
        auto* primaryDisplay = displays.getPrimaryDisplay();
        auto screenArea = primaryDisplay ? primaryDisplay->userArea : juce::Rectangle<int>(0, 0, 1920, 1080);
        int maxScreenHeight = screenArea.getHeight();

        // Make resizable only vertically for JUCE controls (fixed width)
        setResizeLimits(700, 170, 700, maxScreenHeight);

        // Calculate default size to fit visible parameters
        int defaultHeight = 50 + 92 + parameterAreaHeight + 20; // Title + buttons + params + margin
        defaultHeight = juce::jlimit(170, maxScreenHeight, defaultHeight);

        // Restore saved size if requested and available
        restoredWidth = 700; // Fixed width for parameter-only view

        int savedHeight = -1;
        if (restoreSavedSize)
        {
            // Try scoped key first, then fall back to global key for migration from old versions
            savedHeight = getStateProperty("editorHeight", -1);
            if (savedHeight == -1)
                savedHeight = getGlobalProperty("editorHeight", -1);
        }

        // Use saved size if available, otherwise use default
        if (savedHeight > 0)
            restoredHeight = juce::jlimit(170, maxScreenHeight, savedHeight);
        else
            restoredHeight = juce::jlimit(170, maxScreenHeight, defaultHeight);
    }
}

//==============================================================================

void AudioPluginAudioProcessorEditor::onJsfxLoaded()
{
    // Common code path after JSFX is loaded (manually or from saved state)
    // Handles all UI updates and state restoration

    // Update the JSFX file path in state tree for per-JSFX scoping
    processorRef.getAPVTS().state.setProperty("jsfxFilePath", processorRef.getCurrentJSFXPath(), nullptr);

    // Ensure any native window is closed
    destroyJsfxUI();

    // Rebuild UI for the newly loaded JSFX
    rebuildParameterSliders();

    // Update preset list
    updatePresetList();

    // Update title label to show new JSFX name
    updateTitleLabel();

    // Defer JSFX UI preparation to next event loop cycle
    // This ensures @init has been executed and framebuffer is initialized
    juce::MessageManager::callAsync(
        [this]()
        {
            if (processorRef.getSXInstancePtr() != nullptr)
            {
                prepareJsfxUi(false); // Use default size for newly loaded JSFX
                setSize(restoredWidth, restoredHeight);
            }
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

    // Draw separator line above parameters (if visible)
    if (viewport.isVisible() && viewport.getHeight() > 0 && parametersVisible)
    {
        auto viewportBounds = viewport.getBounds();
        g.setColour(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).contrasting(0.2f));
        g.fillRect(viewportBounds.getX(), viewportBounds.getY(), viewportBounds.getWidth(), 1);
    }
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

        // Calculate minimum required width (4 buttons: Unload, Editor, I/O Matrix, About - UI button is hidden)
        int minRequired =
            pluginBrowserWidth + spacing + presetBrowserWidth + spacing + (buttonWidth * 4) + (spacing * 3);

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

        // Layout main buttons on the right (full 30px height) - UI button is hidden
        unloadButton.setBounds(buttonRowArea.removeFromLeft(buttonWidth));
        unloadButton.setVisible(true);
        buttonRowArea.removeFromLeft(spacing);
        editButton.setBounds(buttonRowArea.removeFromLeft(buttonWidth));
        editButton.setVisible(true);
        buttonRowArea.removeFromLeft(spacing);
        // uiButton is hidden (using addChildComponent) - skip it
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

    // Layout parameters above LICE renderer (if both exist)
    // Otherwise, give all remaining space to parameters

    if (jsfxLiceRenderer && jsfxLiceRenderer->isVisible())
    {
        // Calculate parameter area height based on number of visible parameters
        int parameterHeight = parameterSliders.size() * PluginConstants::ParameterSliderHeight;

        if (parameterHeight > 0 && viewport.isVisible() && parametersVisible)
        {
            // Give parameters the calculated height (only if parametersVisible is true)
            auto paramArea = bounds.removeFromTop(parameterHeight);
            viewport.setBounds(paramArea);
        }
        else
        {
            // Hide viewport completely when no visible parameters or parametersVisible is false
            viewport.setBounds(bounds.getX(), bounds.getY(), bounds.getWidth(), 0);
        }

        // LICE renderer gets remaining space (all of bounds when parameters hidden)
        jsfxLiceRenderer->setBounds(bounds);
    }
    else
    {
        // No LICE renderer - give all space to parameters
        viewport.setBounds(bounds);
    }

    // Resize parameter container to match viewport width dynamically
    if (viewport.isVisible() && parameterContainer.isVisible() && parametersVisible)
    {
        // Use actual viewport width minus scrollbar, no artificial minimum
        int viewportInnerWidth = viewport.getWidth() - viewport.getScrollBarThickness();
        int containerWidth = juce::jmax(200, viewportInnerWidth); // Only prevent extreme collapse
        int containerHeight = parameterSliders.size() * PluginConstants::ParameterSliderHeight;
        parameterContainer.setSize(containerWidth, containerHeight);

        // Resize all parameter sliders to match container width
        int y = 0;
        for (auto* slider : parameterSliders)
        {
            slider->setBounds(0, y, containerWidth, PluginConstants::ParameterSliderHeight - 2);
            y += PluginConstants::ParameterSliderHeight;
        }
    }

    // Editor size will be saved in destructor only, not on every resize
}

bool AudioPluginAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // Global keyboard shortcuts:
    // - Shift + / : Focus search field (legacy - cycles through search fields)
    // - Ctrl + F  : Cycle through search fields
    // - F         : Toggle UI visibility (button bar + parameters when both params and GFX present)
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

    // F key - Toggle button bar and parameters visibility
    if (key.getTextCharacter() == 'f' || key.getTextCharacter() == 'F')
    {
        // Only toggle if no modifiers are pressed (to avoid conflict with Ctrl+F)
        if (!key.getModifiers().isAnyModifierKeyDown())
        {
            // If we have both JUCE params and GFX visible, hide/show both button bar and parameters
            // Otherwise just toggle button bar
            bool hasVisibleParams = parameterSliders.size() > 0;
            bool hasGfx = jsfxLiceRenderer && jsfxLiceRenderer->isVisible();

            if (hasVisibleParams && hasGfx)
            {
                // Toggle both button bar and parameters for GFX-only view
                buttonBarVisible = !buttonBarVisible;
                parametersVisible = !parametersVisible;
            }
            else
            {
                // Only toggle button bar (params-only or no JSFX loaded)
                buttonBarVisible = !buttonBarVisible;
            }

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

    // Only create sliders for visible parameters
    int numVisibleParams = 0;
    for (int i = 0; i < numParams; ++i)
    {
        if (processorRef.isJSFXParameterVisible(i))
        {
            auto* slider = new ParameterSlider(processorRef, i);
            parameterSliders.add(slider);
            parameterContainer.addAndMakeVisible(slider);
            numVisibleParams++;
        }
    }

    // Calculate initial size - will be properly sized in resized()
    int containerWidth =
        viewport.getWidth() > 0 ? juce::jmax(200, viewport.getWidth() - viewport.getScrollBarThickness()) : 600;
    int totalHeight = numVisibleParams * PluginConstants::ParameterSliderHeight;

    parameterContainer.setSize(containerWidth, totalHeight);

    int y = 0;
    for (auto* slider : parameterSliders)
    {
        slider->setBounds(0, y, containerWidth, PluginConstants::ParameterSliderHeight - 2);
        y += PluginConstants::ParameterSliderHeight;
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
