#include "PresetWindow.h"
#include "PluginProcessor.h"

//==============================================================================
// PresetWindow Implementation
//==============================================================================

PresetWindow::PresetWindow(AudioPluginAudioProcessor& proc)
    : processor(proc)
    , presetTreeView(proc)
{
    // Set menu title for narrow mode
    setButtonMenuTitle("Presets");

    // Add buttons to the button row (from base class)
    exportButton = &getButtonRow().addButton("Export", [this]() { exportSelectedPresets(); });
    deleteButton = &getButtonRow().addButton("Delete", [this]() { deleteSelectedPresets(); });
    saveButton = &getButtonRow().addButton("Save", [this]() { saveCurrentPreset(); });
    defaultButton = &getButtonRow().addButton("Default", [this]() { resetToDefaults(); });
    setDefaultButton = &getButtonRow().addButton("Set as Default", [this]() { setAsDefaultPreset(); });
    directoriesButton = &getButtonRow().addButton("Directories", [this]() { showDirectoryEditor(); });
    refreshButton = &getButtonRow().addButton("Refresh", [this]() { refreshPresetList(); });

    // Setup tree view
    addAndMakeVisible(presetTreeView);
    presetTreeView.onSelectionChangedCallback = [this]() { updateButtonsForSelection(); };

    // Setup tree view command callback (for Enter key / double-click)
    presetTreeView.onCommand = [this](const juce::Array<juce::TreeViewItem*>& selectedItems)
    {
        if (!selectedItems.isEmpty())
            handlePresetTreeItemSelected(selectedItems[0]);
    };

    setSize(600, 500);
}

PresetWindow::~PresetWindow() = default;

void PresetWindow::setShowManagementButtons(bool show)
{
    if (showManagementButtons == show)
        return;

    showManagementButtons = show;
    setControlsVisible(show);
}

void PresetWindow::visibilityChanged()
{
    if (isVisible())
        refreshPresetList();
}

void PresetWindow::refreshPresetList()
{
    // Read presets from in-memory cache (populated by PresetLoader in background)
    auto presetsTree = processor.getPresetCache().getPresetsTree();

    if (!presetsTree.isValid() || presetsTree.getNumChildren() == 0)
    {
        presetTreeView.loadPresetsFromValueTree(juce::ValueTree());
        getStatusLabel().setText("No presets loaded", juce::dontSendNotification);
        updateButtonsForSelection();
        return;
    }

    // Load presets from cache
    presetTreeView.loadPresetsFromValueTree(presetsTree);

    // Count files and banks
    int fileCount = presetsTree.getNumChildren();
    int bankCount = 0;
    for (int i = 0; i < fileCount; ++i)
        bankCount += presetsTree.getChild(i).getNumChildren();

    getStatusLabel().setText(
        "Loaded " + juce::String(fileCount) + " preset files (" + juce::String(bankCount) + " banks)",
        juce::dontSendNotification
    );

    updateButtonsForSelection();
}

void PresetWindow::exportSelectedPresets()
{
    auto selectedItems = presetTreeView.getSelectedPresetItems();

    // If tree is collapsed and nothing is selected, export ALL presets
    if (selectedItems.isEmpty())
    {
        if (presetTreeView.isInCollapsedMode())
        {
            // Get all presets from the tree
            auto allItems = presetTreeView.getDeepestLevelItems();
            for (auto* item : allItems)
                if (auto* presetItem = dynamic_cast<PresetTreeItem*>(item))
                    selectedItems.add(presetItem);
        }

        if (selectedItems.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "No Presets",
                "No presets available to export.",
                "OK",
                nullptr
            );
            return;
        }
    }

    // Recursively collect all preset items from selected items
    // (if a Directory/File/Bank is selected, get all presets underneath)
    juce::Array<PresetTreeItem*> presetsToExport;
    collectPresetsRecursively(presetsToExport, selectedItems);

    if (presetsToExport.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No Presets",
            "No presets found in selection.",
            "OK",
            nullptr
        );
        return;
    }

    // Ask for export location
    auto chooser = std::make_shared<juce::FileChooser>("Export Presets", juce::File(), "*.rpl");

    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, presetsToExport](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;

            // Ensure .rpl extension
            if (!file.hasFileExtension(".rpl"))
                file = file.withFileExtension(".rpl");

            // Build content from selected items
            juce::String exportContent;

            // Group by file/bank
            std::map<juce::String, std::map<juce::String, std::vector<PresetTreeItem*>>> groupedPresets;

            for (auto* item : presetsToExport)
            {
                if (item->getType() == PresetTreeItem::ItemType::Preset)
                {
                    auto bankKey = item->getFile().getFullPathName() + "|" + item->getBankName();
                    groupedPresets[bankKey][item->getBankName()].push_back(item);
                }
            }

            // Write each bank
            for (const auto& [bankKey, banks] : groupedPresets)
            {
                for (const auto& [bankName, presets] : banks)
                {
                    exportContent += "<REAPER_PRESET_LIBRARY `JS: " + bankName + "`\n";

                    for (auto* preset : presets)
                    {
                        exportContent += "  <PRESET `" + preset->getPresetName() + "`\n";
                        exportContent += preset->getPresetData();
                        exportContent += "\n  >\n";
                    }

                    exportContent += ">\n\n";
                }
            }

            if (file.replaceWithText(exportContent))
            {
                getStatusLabel().setText(
                    "Exported " + juce::String(presetsToExport.size()) + " presets",
                    juce::dontSendNotification
                );
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Export Failed",
                    "Failed to export presets.",
                    "OK",
                    nullptr
                );
            }
        }
    );
}

void PresetWindow::deleteSelectedPresets()
{
    auto selectedItems = presetTreeView.getSelectedPresetItems();

    // If tree is collapsed and nothing is selected, delete ALL presets
    if (selectedItems.isEmpty())
    {
        if (presetTreeView.isInCollapsedMode())
        {
            // Get all presets from the tree
            auto allItems = presetTreeView.getDeepestLevelItems();
            for (auto* item : allItems)
                if (auto* presetItem = dynamic_cast<PresetTreeItem*>(item))
                    selectedItems.add(presetItem);
        }

        if (selectedItems.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "No Presets",
                "No presets available to delete.",
                "OK",
                nullptr
            );
            return;
        }
    }

    // Recursively collect all preset items from selected items
    // (if a Directory/File/Bank is selected, get all presets underneath)
    juce::Array<PresetTreeItem*> presetsToDelete;
    collectPresetsRecursively(presetsToDelete, selectedItems);

    if (presetsToDelete.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No Presets",
            "No presets found in selection.",
            "OK",
            nullptr
        );
        return;
    }

    // Count what will be deleted - now we're working with actual preset items
    // Group by file to show more meaningful counts
    std::set<juce::String> uniqueFiles;
    for (auto* item : presetsToDelete)
        if (item->getType() == PresetTreeItem::ItemType::Preset)
            uniqueFiles.insert(item->getFile().getFullPathName());

    juce::String message = "Are you sure you want to delete:\n";
    message += juce::String(presetsToDelete.size()) + " preset(s)";
    if (uniqueFiles.size() > 1)
        message += " from " + juce::String((int)uniqueFiles.size()) + " file(s)";
    message += "\n\nThis action cannot be undone.";

    auto result = juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        "Confirm Delete",
        message,
        "Delete",
        "Cancel",
        nullptr,
        nullptr
    );

    if (result == 0)
        return;

    int deletedPresets = 0;

    // For individual presets, we need to rewrite the file without those presets
    // Group presets to delete by file
    std::map<juce::String, std::vector<PresetTreeItem*>> presetsToDeleteByFile;

    for (auto* item : presetsToDelete)
        if (item->getType() == PresetTreeItem::ItemType::Preset)
            presetsToDeleteByFile[item->getFile().getFullPathName()].push_back(item);

    // Process each file
    for (const auto& [filePath, presetsInFile] : presetsToDeleteByFile)
    {
        juce::File file(filePath);
        auto content = file.loadFileAsString();

        // Remove each preset from content
        for (auto* preset : presetsInFile)
        {
            // Find and remove this preset
            juce::String searchPattern = "<PRESET `" + preset->getPresetName() + "`";
            int presetStart = content.indexOf(searchPattern);

            if (presetStart >= 0)
            {
                // Find the closing >
                int depth = 1;
                int pos = content.indexOf(presetStart, ">") + 1;
                int presetEnd = -1;

                while (pos < content.length() && depth > 0)
                {
                    if (content[pos] == '<')
                        depth++;
                    else if (content[pos] == '>')
                    {
                        depth--;
                        if (depth == 0)
                        {
                            presetEnd = pos + 1;
                            break;
                        }
                    }
                    pos++;
                }

                if (presetEnd >= 0)
                {
                    content = content.substring(0, presetStart) + content.substring(presetEnd);
                    deletedPresets++;
                }
            }
        }

        // Write back the modified content
        file.replaceWithText(content);
    }

    getStatusLabel().setText("Deleted " + juce::String(deletedPresets) + " preset(s)", juce::dontSendNotification);

    refreshPresetList();
}

void PresetWindow::saveCurrentPreset()
{
    // Check if JSFX is loaded
    juce::String jsfxPath = processor.getCurrentJSFXPath();
    if (jsfxPath.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "No JSFX Loaded",
            "Please load a JSFX before saving a preset.",
            "OK",
            nullptr
        );
        return;
    }

    // Get default bank name from currently selected preset or use "User"
    juce::String defaultBankName = "User";
    juce::String defaultPresetName = "New Preset";

    auto selectedItems = presetTreeView.getSelectedPresetItems();
    if (!selectedItems.isEmpty())
    {
        auto* firstItem = selectedItems[0];
        if (firstItem->getType() == PresetTreeItem::ItemType::Preset)
        {
            defaultBankName = firstItem->getBankName();
            defaultPresetName = firstItem->getPresetName();
        }
        else if (firstItem->getType() == PresetTreeItem::ItemType::Bank)
        {
            defaultBankName = firstItem->getBankName();
        }
    }

    // Create a dialog to get bank and preset name using AlertWindow async
    auto* window =
        new juce::AlertWindow("Save Preset", "Enter bank and preset name:", juce::MessageBoxIconType::QuestionIcon);

    window->addTextEditor("bankName", defaultBankName, "Bank Name:");
    window->addTextEditor("presetName", defaultPresetName, "Preset Name:");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [this, window](int result)
            {
                if (result == 1)
                {
                    juce::String bankName = window->getTextEditorContents("bankName").trim();
                    juce::String presetName = window->getTextEditorContents("presetName").trim();

                    if (bankName.isEmpty())
                        bankName = "User";

                    if (presetName.isEmpty())
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Invalid Name",
                            "Preset name cannot be empty.",
                            "OK",
                            nullptr
                        );
                        return;
                    }

                    // Save the preset
                    if (processor.saveUserPreset(bankName, presetName))
                    {
                        getStatusLabel().setText("Saved preset: " + presetName, juce::dontSendNotification);
                        // The preset loader will automatically refresh when saveUserPreset triggers it
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Save Failed",
                            "Failed to save preset. Please check that the JSFX is loaded correctly.",
                            "OK",
                            nullptr
                        );
                    }
                }
            }
        ),
        true
    );
}

void PresetWindow::showDirectoryEditor()
{
    auto directories = getPresetDirectories();

    auto* editor = new PresetDirectoryEditor(
        directories,
        [this](const juce::StringArray& newDirectories)
        {
            setPresetDirectories(newDirectories);
            refreshPresetList();
        }
    );

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(editor);
    options.dialogTitle = "Preset Directories";
    options.resizable = true;
    options.useNativeTitleBar = true;

    auto* window = options.launchAsync();
    if (window != nullptr)
        window->centreWithSize(600, 400);
}

void PresetWindow::collectPresetsRecursively(
    juce::Array<PresetTreeItem*>& presets,
    const juce::Array<PresetTreeItem*>& selectedItems
)
{
    for (auto* item : selectedItems)
    {
        if (item->getType() == PresetTreeItem::ItemType::Preset)
        {
            // This is already a preset - add it directly
            presets.add(item);
        }
        else
        {
            // This is a parent item (Directory, File, or Bank) - collect all presets underneath
            std::function<void(juce::TreeViewItem*)> collectFromItem = [&](juce::TreeViewItem* treeItem)
            {
                if (auto* presetItem = dynamic_cast<PresetTreeItem*>(treeItem))
                {
                    if (presetItem->getType() == PresetTreeItem::ItemType::Preset)
                        presets.add(presetItem);
                }

                // Recurse into children
                for (int i = 0; i < treeItem->getNumSubItems(); ++i)
                    collectFromItem(treeItem->getSubItem(i));
            };

            collectFromItem(item);
        }
    }
}

void PresetWindow::updateButtonsForSelection()
{
    auto selectedItems = presetTreeView.getSelectedPresetItems();
    bool hasSelection = !selectedItems.isEmpty();

    // In collapsed/autohide mode, enable buttons if ANY presets exist (not just selected)
    // In expanded mode, only enable if there's a selection
    bool hasAnyPresets = !presetTreeView.getDeepestLevelItems().isEmpty();
    bool isCollapsed = presetTreeView.isInCollapsedMode();

    bool shouldEnableButtons = hasSelection || (isCollapsed && hasAnyPresets);

    exportButton->setEnabled(shouldEnableButtons);
    deleteButton->setEnabled(shouldEnableButtons);
}

juce::StringArray PresetWindow::getPresetDirectories() const
{
    // Get current JSFX filename (without extension) for per-JSFX storage
    juce::String jsfxPath = processor.getCurrentJSFXPath();
    if (jsfxPath.isEmpty())
        return juce::StringArray();

    juce::File jsfxFile(jsfxPath);
    juce::String jsfxName = jsfxFile.getFileNameWithoutExtension();

    // Load from global storage using JSFX name as key
    auto& state = processor.getAPVTS().state;
    auto propKey = "presetDirectories_" + jsfxName;
    auto dirString = state.getProperty(propKey, "").toString();

    if (dirString.isEmpty())
        return juce::StringArray();

    juce::StringArray directories;
    directories.addLines(dirString);
    return directories;
}

void PresetWindow::setPresetDirectories(const juce::StringArray& directories)
{
    // Get current JSFX filename (without extension) for per-JSFX storage
    juce::String jsfxPath = processor.getCurrentJSFXPath();
    if (jsfxPath.isEmpty())
        return;

    juce::File jsfxFile(jsfxPath);
    juce::String jsfxName = jsfxFile.getFileNameWithoutExtension();

    // Save to global storage using JSFX name as key
    auto& state = processor.getAPVTS().state;
    auto propKey = "presetDirectories_" + jsfxName;
    state.setProperty(propKey, directories.joinIntoString("\n"), nullptr);
}

//==============================================================================
// PresetDirectoryEditor Implementation
//==============================================================================

PresetDirectoryEditor::PresetDirectoryEditor(
    const juce::StringArray& currentDirectories,
    std::function<void(const juce::StringArray&)> onSave
)
    : saveCallback(onSave)
{
    setLookAndFeel(&sharedLookAndFeel->lf);

    addAndMakeVisible(instructionsLabel);
    instructionsLabel.setMultiLine(true);
    instructionsLabel.setReadOnly(true);
    instructionsLabel.setScrollbarsShown(false);
    instructionsLabel.setCaretVisible(false);
    instructionsLabel.setPopupMenuEnabled(true);
    instructionsLabel.setText(
        "Enter preset search directories (one per line):\n"
        "The preset browser will scan these directories for .rpl files."
    );
    instructionsLabel.setFont(juce::FontOptions(12.0f));
    instructionsLabel.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    instructionsLabel.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);

    addAndMakeVisible(directoryEditor);
    directoryEditor.setMultiLine(true);
    directoryEditor.setReturnKeyStartsNewLine(true);
    directoryEditor.setScrollbarsShown(true);
    directoryEditor.setFont(juce::FontOptions(12.0f));
    directoryEditor.setText(currentDirectories.joinIntoString("\n"));

    addAndMakeVisible(saveButton);
    saveButton.setButtonText("Save");
    saveButton.onClick = [this]() { saveAndClose(); };

    addAndMakeVisible(cancelButton);
    cancelButton.setButtonText("Cancel");
    cancelButton.onClick = [this]() { cancel(); };

    setSize(600, 400);
}

PresetDirectoryEditor::~PresetDirectoryEditor()
{
    setLookAndFeel(nullptr);
}

void PresetDirectoryEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void PresetDirectoryEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    instructionsLabel.setBounds(bounds.removeFromTop(40));
    bounds.removeFromTop(5);

    auto buttonBar = bounds.removeFromBottom(30);
    cancelButton.setBounds(buttonBar.removeFromRight(80));
    buttonBar.removeFromRight(5);
    saveButton.setBounds(buttonBar.removeFromRight(80));
    bounds.removeFromBottom(10);

    directoryEditor.setBounds(bounds);
}

void PresetDirectoryEditor::saveAndClose()
{
    juce::StringArray directories;
    directories.addLines(directoryEditor.getText());

    // Remove empty lines
    directories.removeEmptyStrings();

    if (saveCallback)
        saveCallback(directories);

    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(1);
}

void PresetWindow::handlePresetTreeItemSelected(juce::TreeViewItem* item)
{
    if (!item)
        return;

    // Cast to PresetTreeItem to access preset data
    if (auto* presetItem = dynamic_cast<PresetTreeItem*>(item))
    {
        // Only load if it's an actual preset (not a directory/file/bank)
        if (presetItem->getType() == PresetTreeItem::ItemType::Preset)
        {
            juce::String bankName = presetItem->getBankName();
            juce::String presetName = presetItem->getPresetName();
            juce::String presetData = presetItem->getPresetData();

            // Track for delete operations
            currentPresetBankName = bankName;
            currentPresetName = presetName;

            // Notify callback if set
            if (onPresetSelected)
                onPresetSelected(bankName, presetName, presetData);

            // Also load directly via processor
            processor.loadPresetFromBase64(presetData);
        }
    }
}

void PresetWindow::resetToDefaults()
{
    // Check if JSFX is loaded
    juce::String jsfxPath = processor.getCurrentJSFXPath();
    if (jsfxPath.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "No JSFX Loaded",
            "Please load a JSFX before resetting to defaults.",
            "OK",
            nullptr
        );
        return;
    }

    // Check if a default preset exists
    if (processor.hasDefaultPreset())
    {
        auto result = juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::QuestionIcon,
            "Reset to Defaults",
            "A default preset exists for this JSFX. Do you want to load it?\n\n"
            "Yes: Load the saved default preset\n"
            "No: Reset to JSFX parameter defaults",
            "Yes",
            "No",
            nullptr,
            nullptr
        );

        if (result == 0)
        {
            // User clicked No - reset to JSFX defaults
            processor.resetToDefaults();
            getStatusLabel().setText("Reset to JSFX parameter defaults", juce::dontSendNotification);
        }
        else
        {
            // User clicked Yes - load default preset
            processor.resetToDefaults(); // This will load the default preset
            getStatusLabel().setText("Loaded default preset", juce::dontSendNotification);
        }
    }
    else
    {
        // No default preset - just reset to JSFX defaults
        processor.resetToDefaults();
        getStatusLabel().setText("Reset to JSFX parameter defaults", juce::dontSendNotification);
    }
}

void PresetWindow::setAsDefaultPreset()
{
    // Check if JSFX is loaded
    juce::String jsfxPath = processor.getCurrentJSFXPath();
    if (jsfxPath.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "No JSFX Loaded",
            "Please load a JSFX before setting a default preset.",
            "OK",
            nullptr
        );
        return;
    }

    // Check if default preset already exists
    if (processor.hasDefaultPreset())
    {
        auto result = juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::QuestionIcon,
            "Overwrite Default Preset",
            "A default preset already exists for this JSFX.\n\n"
            "Do you want to overwrite it with the current parameter state?",
            "Overwrite",
            "Cancel",
            nullptr,
            nullptr
        );

        if (result == 0)
            return;
    }

    // Save current state as default
    if (processor.setAsDefaultPreset())
    {
        getStatusLabel().setText("Saved current state as default preset", juce::dontSendNotification);
        refreshPresetList(); // Refresh to show the new default preset
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Save Failed",
            "Failed to save default preset. Please check that the JSFX is loaded correctly.",
            "OK",
            nullptr
        );
    }
}

void PresetDirectoryEditor::cancel()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}
