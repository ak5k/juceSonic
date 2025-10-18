#include "PresetWindow.h"
#include "PluginProcessor.h"

//==============================================================================
// PresetWindow Implementation
//==============================================================================

PresetWindow::PresetWindow(AudioPluginAudioProcessor& proc)
    : processor(proc)
    , presetTreeView(proc)
{
    setLookAndFeel(&sharedLookAndFeel->lf);

    // Setup buttons
    addAndMakeVisible(importButton);
    importButton.onClick = [this]() { importPresetFile(); };

    addAndMakeVisible(exportButton);
    exportButton.onClick = [this]() { exportSelectedPresets(); };

    addAndMakeVisible(deleteButton);
    deleteButton.onClick = [this]() { deleteSelectedPresets(); };

    addAndMakeVisible(directoriesButton);
    directoriesButton.onClick = [this]() { showDirectoryEditor(); };

    addAndMakeVisible(refreshButton);
    refreshButton.onClick = [this]() { refreshPresetList(); };

    // Setup tree view
    addAndMakeVisible(presetTreeView);
    presetTreeView.onSelectionChangedCallback = [this]() { updateButtonsForSelection(); };

    // Setup status label
    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setFont(juce::FontOptions(12.0f));

    setSize(600, 500);
}

PresetWindow::~PresetWindow()
{
    setLookAndFeel(nullptr);
}

void PresetWindow::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void PresetWindow::resized()
{
    auto bounds = getLocalBounds().reduced(4);

    // Top button row
    auto topButtons = bounds.removeFromTop(30);
    importButton.setBounds(topButtons.removeFromLeft(80));
    topButtons.removeFromLeft(4);
    exportButton.setBounds(topButtons.removeFromLeft(80));
    topButtons.removeFromLeft(4);
    deleteButton.setBounds(topButtons.removeFromLeft(80));
    topButtons.removeFromLeft(4);
    topButtons.removeFromLeft(20); // Spacer
    directoriesButton.setBounds(topButtons.removeFromLeft(100));
    topButtons.removeFromLeft(4);
    refreshButton.setBounds(topButtons.removeFromLeft(80));

    bounds.removeFromTop(4);

    // Status label at bottom
    statusLabel.setBounds(bounds.removeFromBottom(20));
    bounds.removeFromBottom(4);

    // Tree view fills remaining space
    presetTreeView.setBounds(bounds);
}

void PresetWindow::visibilityChanged()
{
    if (isVisible())
        refreshPresetList();
}

void PresetWindow::refreshPresetList()
{
    // Read presets from APVTS state (populated by PresetLoader in background)
    auto& state = processor.getAPVTS().state;
    auto presetsNode = state.getChildWithName("presets");

    if (!presetsNode.isValid() || presetsNode.getNumChildren() == 0)
    {
        presetTreeView.loadPresetsFromValueTree(juce::ValueTree());
        statusLabel.setText("No presets loaded", juce::dontSendNotification);
        updateButtonsForSelection();
        return;
    }

    // Load presets from APVTS ValueTree
    presetTreeView.loadPresetsFromValueTree(presetsNode);

    // Count files and banks
    int fileCount = presetsNode.getNumChildren();
    int bankCount = 0;
    for (int i = 0; i < fileCount; ++i)
        bankCount += presetsNode.getChild(i).getNumChildren();

    statusLabel.setText(
        "Loaded " + juce::String(fileCount) + " preset files (" + juce::String(bankCount) + " banks)",
        juce::dontSendNotification
    );

    updateButtonsForSelection();
}

void PresetWindow::importPresetFile()
{
    auto chooser = std::make_shared<juce::FileChooser>("Import Preset File", juce::File(), "*.rpl");

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (!file.existsAsFile())
                return;

            // Get current JSFX filename (without extension)
            juce::String jsfxPath = processor.getCurrentJSFXPath();
            if (jsfxPath.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Import Failed",
                    "No JSFX loaded. Please load a JSFX before importing presets.",
                    "OK",
                    nullptr
                );
                return;
            }

            juce::File jsfxFile(jsfxPath);
            juce::String jsfxFilename = jsfxFile.getFileNameWithoutExtension();

            // Build target directory: <appdata>/juceSonic/data/local/<jsfx-filename>/
            auto targetDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                 .getChildFile("juceSonic")
                                 .getChildFile("data")
                                 .getChildFile("local")
                                 .getChildFile(jsfxFilename);

            // Create directory structure if needed
            if (!targetDir.exists() && !targetDir.createDirectory())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Import Failed",
                    "Failed to create preset directory: " + targetDir.getFullPathName(),
                    "OK",
                    nullptr
                );
                return;
            }

            // Target file path
            auto targetFile = targetDir.getChildFile(file.getFileName());

            // Check if file exists and ask to overwrite
            if (targetFile.existsAsFile())
            {
                auto result = juce::AlertWindow::showOkCancelBox(
                    juce::MessageBoxIconType::QuestionIcon,
                    "File Exists",
                    "A preset file with this name already exists:\n" + targetFile.getFullPathName() + "\n\nOverwrite?",
                    "Overwrite",
                    "Cancel",
                    nullptr,
                    nullptr
                );

                if (result == 0) // User clicked Cancel
                    return;
            }

            // Copy file to target directory
            if (file.copyFileTo(targetFile))
            {
                statusLabel.setText(
                    "Imported: " + file.getFileName() + " to " + jsfxFilename,
                    juce::dontSendNotification
                );

                refreshPresetList();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Import Failed",
                    "Failed to copy file from:\n" + file.getFullPathName() + "\n\nto:\n" + targetFile.getFullPathName(),
                    "OK",
                    nullptr
                );
            }
        }
    );
}

void PresetWindow::exportSelectedPresets()
{
    auto selectedItems = presetTreeView.getSelectedPresetItems();
    if (selectedItems.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No Selection",
            "Please select presets to export.",
            "OK",
            nullptr
        );
        return;
    }

    // Ask for export location
    auto chooser = std::make_shared<juce::FileChooser>("Export Presets", juce::File(), "*.rpl");

    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, selectedItems](const juce::FileChooser& fc)
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

            for (auto* item : selectedItems)
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
                statusLabel.setText(
                    "Exported " + juce::String(selectedItems.size()) + " presets",
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
    if (selectedItems.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No Selection",
            "Please select presets to delete.",
            "OK",
            nullptr
        );
        return;
    }

    // Count what will be deleted
    int fileCount = 0;
    int presetCount = 0;

    for (auto* item : selectedItems)
        if (item->getType() == PresetTreeItem::ItemType::File)
            fileCount++;
        else if (item->getType() == PresetTreeItem::ItemType::Preset)
            presetCount++;

    juce::String message = "Are you sure you want to delete:\n";
    if (fileCount > 0)
        message += juce::String(fileCount) + " preset file(s)\n";
    if (presetCount > 0)
        message += juce::String(presetCount) + " preset(s)\n";

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

    int deletedFiles = 0;
    int deletedPresets = 0;

    // Delete files
    for (auto* item : selectedItems)
    {
        if (item->getType() == PresetTreeItem::ItemType::File)
        {
            if (item->getFile().deleteFile())
                deletedFiles++;
        }
    }

    // For individual presets, we need to rewrite the file without those presets
    // Group presets to delete by file
    std::map<juce::String, std::vector<PresetTreeItem*>> presetsToDeleteByFile;

    for (auto* item : selectedItems)
        if (item->getType() == PresetTreeItem::ItemType::Preset)
            presetsToDeleteByFile[item->getFile().getFullPathName()].push_back(item);

    // Process each file
    for (const auto& [filePath, presetsToDelete] : presetsToDeleteByFile)
    {
        juce::File file(filePath);
        auto content = file.loadFileAsString();

        // Remove each preset from content
        for (auto* preset : presetsToDelete)
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

    statusLabel.setText(
        "Deleted " + juce::String(deletedFiles) + " files, " + juce::String(deletedPresets) + " presets",
        juce::dontSendNotification
    );

    refreshPresetList();
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

void PresetWindow::updateButtonsForSelection()
{
    auto selectedItems = presetTreeView.getSelectedPresetItems();
    bool hasSelection = !selectedItems.isEmpty();

    exportButton.setEnabled(hasSelection);
    deleteButton.setEnabled(hasSelection);
}

juce::StringArray PresetWindow::getPresetDirectories() const
{
    // Load from processor's APVTS
    auto& state = processor.getAPVTS().state;
    auto dirString = state.getProperty("presetDirectories", "").toString();

    if (dirString.isEmpty())
    {
        // Return empty array - the default install root will be added automatically
        return juce::StringArray();
    }

    juce::StringArray directories;
    directories.addLines(dirString);
    return directories;
}

void PresetWindow::setPresetDirectories(const juce::StringArray& directories)
{
    auto& state = processor.getAPVTS().state;
    state.setProperty("presetDirectories", directories.joinIntoString("\n"), nullptr);
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

void PresetDirectoryEditor::cancel()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}
