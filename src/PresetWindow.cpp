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
    wasdButton = &getButtonRow().addButton("WASD", [this]() { setWASDMode(!wasdModeEnabled); });
    exportButton = &getButtonRow().addButton("Export", [this]() { exportSelectedPresets(); });
    deleteButton = &getButtonRow().addButton("Delete", [this]() { deleteSelectedPresets(); });
    saveButton = &getButtonRow().addButton("Save", [this]() { saveCurrentPreset(); });
    defaultButton = &getButtonRow().addButton("Default", [this]() { resetToDefaults(); });
    setDefaultButton = &getButtonRow().addButton("Set as Default", [this]() { setAsDefaultPreset(); });
    directoriesButton = &getButtonRow().addButton("Directories", [this]() { showDirectoryEditor(); });
    refreshButton = &getButtonRow().addButton("Refresh", [this]() { refreshPresetList(); });

    // Initialize WASD button appearance
    setWASDMode(false);

    // Setup tree view
    addAndMakeVisible(presetTreeView);
    presetTreeView.onSelectionChangedCallback = [this]() { updateButtonsForSelection(); };

    // Setup tree view command callback (for Enter key / double-click)
    presetTreeView.onCommand = [this](const juce::Array<juce::TreeViewItem*>& selectedItems)
    {
        // Cache selection before command processing (which might clear tree selection)
        if (!selectedItems.isEmpty())
        {
            cacheSelection(selectedItems);
            handlePresetTreeItemSelected(selectedItems[0]);
        }
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

    // If current selection is empty, use cached selection
    if (selectedItems.isEmpty())
    {
        for (auto* item : getCachedSelection())
            if (auto* presetItem = dynamic_cast<PresetTreeItem*>(item))
                selectedItems.add(presetItem);
    }

    // Require at least one item to be selected
    if (selectedItems.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No Selection",
            "Please select one or more presets, banks, or folders to export.",
            "OK",
            nullptr
        );
        return;
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
                clearCachedSelection();
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

    // If current selection is empty, use cached selection
    if (selectedItems.isEmpty())
    {
        for (auto* item : getCachedSelection())
            if (auto* presetItem = dynamic_cast<PresetTreeItem*>(item))
                selectedItems.add(presetItem);
    }

    // Require at least one item to be selected
    if (selectedItems.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No Selection",
            "Please select one or more presets, banks, or folders to delete.",
            "OK",
            nullptr
        );
        return;
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
    {
        if (item->getType() == PresetTreeItem::ItemType::Preset)
        {
            auto filePath = item->getFile().getFullPathName();
            uniqueFiles.insert(filePath);
        }
    }

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

    if (result != 0) // 0 means Delete was clicked, non-zero means Cancel
        return;

    int deletedPresets = 0;
    int deletedBanks = 0;
    int deletedFiles = 0;

    // Group items to delete by type and file
    std::set<juce::String> filesToDelete;                                       // Entire .rpl files to delete
    std::map<juce::String, std::vector<PresetTreeItem*>> banksToDeleteByFile;   // Banks within files
    std::map<juce::String, std::vector<PresetTreeItem*>> presetsToDeleteByFile; // Individual presets

    for (auto* item : presetsToDelete)
    {
        auto type = item->getType();
        auto filePath = item->getFile().getFullPathName();

        if (type == PresetTreeItem::ItemType::File)
        {
            // Delete the entire file
            filesToDelete.insert(filePath);
        }
        else if (type == PresetTreeItem::ItemType::Bank)
        {
            // Delete a bank from the file
            banksToDeleteByFile[filePath].push_back(item);
        }
        else if (type == PresetTreeItem::ItemType::Preset)
        {
            // Delete an individual preset from the file
            presetsToDeleteByFile[filePath].push_back(item);
        }
    }

    // 1. Delete entire files
    for (const auto& filePath : filesToDelete)
    {
        juce::File file(filePath);
        if (file.deleteFile())
        {
            deletedFiles++;
        }
        else
        {
        }
    }

    // 2. Delete banks from files
    for (const auto& [filePath, banksInFile] : banksToDeleteByFile)
    {
        juce::File file(filePath);
        auto content = file.loadFileAsString();

        // Remove each bank from content
        for (auto* bank : banksInFile)
        {
            // Find and remove this bank
            juce::String searchPattern = "<BANK `" + bank->getBankName() + "`";
            int bankStart = content.indexOf(searchPattern);

            if (bankStart >= 0)
            {
                // Find the closing >
                int depth = 1;
                int pos = content.indexOf(bankStart, ">") + 1;
                int bankEnd = -1;

                while (pos < content.length() && depth > 0)
                {
                    if (content[pos] == '<')
                        depth++;
                    else if (content[pos] == '>')
                    {
                        depth--;
                        if (depth == 0)
                        {
                            bankEnd = pos + 1;
                            break;
                        }
                    }
                    pos++;
                }

                if (bankEnd >= 0)
                {
                    content = content.substring(0, bankStart) + content.substring(bankEnd);
                    deletedBanks++;
                }
                else
                {
                }
            }
            else
            {
            }
        }

        // Check if the file still has any presets or banks left
        bool hasContent = content.contains("<PRESET") || content.contains("<BANK");

        if (!hasContent)
        {
            // File is now empty (only has header), delete it
            if (file.deleteFile())
            {
                deletedFiles++;
            }
            else
            {
            }
        }
        else
        {
            // Write back the modified content
            bool writeSuccess = file.replaceWithText(content);
        }
    }

    // 3. Delete individual presets from files
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
                else
                {
                }
            }
            else
            {
            }
        }

        // Check if the file still has any presets or banks left
        bool hasContent = content.contains("<PRESET") || content.contains("<BANK");

        if (!hasContent)
        {
            // File is now empty (only has header), delete it
            if (file.deleteFile())
            {
                deletedFiles++;
            }
            else
            {
            }
        }
        else
        {
            // Write back the modified content
            bool writeSuccess = file.replaceWithText(content);
        }
    }

    // Build status message
    juce::StringArray messages;
    if (deletedFiles > 0)
        messages.add(juce::String(deletedFiles) + " file(s)");
    if (deletedBanks > 0)
        messages.add(juce::String(deletedBanks) + " bank(s)");
    if (deletedPresets > 0)
        messages.add(juce::String(deletedPresets) + " preset(s)");

    juce::String statusMessage = "Deleted ";
    statusMessage += messages.joinIntoString(", ");

    getStatusLabel().setText(statusMessage, juce::dontSendNotification);

    clearCachedSelection();

    // Trigger preset refresh in background
    processor.refreshPresets();
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
    // Use getSelectedItems() to work directly with the tree view selection
    auto genericSelectedItems = presetTreeView.getSelectedItems();

    // Cache selection when items are selected (don't clear on deselection)
    if (!genericSelectedItems.isEmpty())
        cacheSelection(genericSelectedItems);

    // Button enabling logic:
    // Enable only if there's a current selection OR cached selection
    // No special fallback behavior - user must explicitly select items
    bool hasCachedSelection = !getCachedSelection().isEmpty();
    bool hasSelection = !genericSelectedItems.isEmpty();
    bool shouldEnableButtons = hasSelection || hasCachedSelection;

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
    directoryEditor.setTextToShowWhenEmpty("C:\\PELIT\\FF7", juce::Colours::grey);
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

void PresetWindow::setWASDMode(bool enabled)
{
    wasdModeEnabled = enabled;

    if (wasdButton)
    {
        // Update button appearance to show toggle state
        wasdButton->setToggleState(enabled, juce::dontSendNotification);
        wasdButton->setColour(juce::TextButton::buttonOnColourId, juce::Colours::darkgreen);
    }
}

void PresetWindow::navigateToNextPreset()
{
    navigatePresetJump(1);
}

void PresetWindow::navigateToPreviousPreset()
{
    navigatePresetJump(-1);
}

void PresetWindow::navigatePresetJump(int count)
{
    // Get all preset items in order
    auto allPresets = presetTreeView.getDeepestLevelItems();

    if (allPresets.isEmpty())
        return;

    // Find currently selected preset
    auto selectedItems = presetTreeView.getSelectedItems();
    int currentIndex = -1;

    if (!selectedItems.isEmpty())
    {
        for (int i = 0; i < allPresets.size(); ++i)
        {
            if (allPresets[i] == selectedItems[0])
            {
                currentIndex = i;
                break;
            }
        }
    }

    // Calculate new index
    int newIndex;
    if (currentIndex < 0)
    {
        // No selection - start from first if going forward, last if going backward
        newIndex = (count > 0) ? 0 : allPresets.size() - 1;
    }
    else
    {
        newIndex = currentIndex + count;

        // Wrap around
        if (newIndex < 0)
            newIndex = allPresets.size() + (newIndex % allPresets.size());
        else if (newIndex >= allPresets.size())
            newIndex = newIndex % allPresets.size();
    }

    // Select and load the new preset
    if (newIndex >= 0 && newIndex < allPresets.size())
    {
        if (auto* presetItem = dynamic_cast<PresetTreeItem*>(allPresets[newIndex]))
            selectAndLoadPresetItem(presetItem);
    }
}

void PresetWindow::selectAndLoadPresetItem(PresetTreeItem* item)
{
    if (!item)
        return;

    // Deselect all other items
    presetTreeView.getTreeView().clearSelectedItems();

    // Select the new item
    item->setSelected(true, true);

    // Ensure the item is visible by expanding parent items
    auto* parent = item->getParentItem();
    while (parent)
    {
        parent->setOpen(true);
        parent = parent->getParentItem();
    }

    // Scroll to make the item visible
    presetTreeView.getTreeView().scrollToKeepItemVisible(item);

    // Load the preset
    handlePresetTreeItemSelected(item);
}

void PresetDirectoryEditor::cancel()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}
