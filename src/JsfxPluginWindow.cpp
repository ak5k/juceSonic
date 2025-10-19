#include "JsfxPluginWindow.h"
#include "PluginProcessor.h"

//==============================================================================
// JsfxPluginWindow Implementation
//==============================================================================

JsfxPluginWindow::JsfxPluginWindow(AudioPluginAudioProcessor& proc)
    : processor(proc)
    , pluginTreeView(proc)
{
    // Add buttons to the button row (from base class)
    // Load button reuses the same handler as Enter key / double-click
    loadButton = &getButtonRow().addButton(
        "Load",
        [this]()
        {
            auto selectedItems = pluginTreeView.getSelectedItems();
            if (!selectedItems.isEmpty())
                handlePluginTreeItemSelected(selectedItems[0]);
        }
    );
    deleteButton = &getButtonRow().addButton("Delete", [this]() { deleteSelectedPlugins(); });
    directoriesButton = &getButtonRow().addButton("Directories", [this]() { showDirectoryEditor(); });
    refreshButton = &getButtonRow().addButton("Refresh", [this]() { refreshPluginList(); });

    // Setup tree view
    addAndMakeVisible(pluginTreeView);
    pluginTreeView.onSelectionChangedCallback = [this]() { updateButtonsForSelection(); };

    // Setup tree view command callback (for Enter key / double-click)
    pluginTreeView.onCommand = [this](const juce::Array<juce::TreeViewItem*>& selectedItems)
    {
        if (!selectedItems.isEmpty())
            handlePluginTreeItemSelected(selectedItems[0]);
    };

    setSize(600, 500);
}

JsfxPluginWindow::~JsfxPluginWindow() = default;

void JsfxPluginWindow::setShowManagementButtons(bool show)
{
    if (showManagementButtons == show)
        return;

    showManagementButtons = show;
    setControlsVisible(show);
}

void JsfxPluginWindow::visibilityChanged()
{
    if (isVisible())
        refreshPluginList();
}

void JsfxPluginWindow::refreshPluginList()
{
    auto directories = getPluginDirectories();
    pluginTreeView.loadPlugins(directories);

    // Count plugins
    int totalPlugins = 0;
    if (pluginTreeView.getRootItem())
    {
        for (int i = 0; i < pluginTreeView.getRootItem()->getNumSubItems(); ++i)
        {
            auto* categoryItem = pluginTreeView.getRootItem()->getSubItem(i);
            totalPlugins += categoryItem->getNumSubItems();
        }
    }

    getStatusLabel().setText("Found " + juce::String(totalPlugins) + " plugin(s)", juce::dontSendNotification);

    updateButtonsForSelection();
}

void JsfxPluginWindow::deleteSelectedPlugins()
{
    auto selectedItems = pluginTreeView.getSelectedPluginItems();
    if (selectedItems.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No Selection",
            "Please select plugins to delete.",
            "OK",
            nullptr
        );
        return;
    }

    // Count what will be deleted (only plugin files, not categories)
    int pluginCount = 0;
    for (auto* item : selectedItems)
        if (item->getType() == JsfxPluginTreeItem::ItemType::Plugin)
            pluginCount++;

    if (pluginCount == 0)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No Plugins Selected",
            "Please select plugin files (not categories) to delete.",
            "OK",
            nullptr
        );
        return;
    }

    juce::String message = "Are you sure you want to delete "
                         + juce::String(pluginCount)
                         + " plugin(s)?\n\n"
                           "This action cannot be undone.";

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

    int deletedCount = 0;

    // Delete plugins
    for (auto* item : selectedItems)
    {
        if (item->getType() == JsfxPluginTreeItem::ItemType::Plugin)
        {
            auto pluginFile = item->getFile();
            if (pluginFile.existsAsFile() && pluginFile.deleteFile())
                deletedCount++;
        }
    }

    getStatusLabel().setText("Deleted " + juce::String(deletedCount) + " plugin(s)", juce::dontSendNotification);

    refreshPluginList();
}

void JsfxPluginWindow::showDirectoryEditor()
{
    auto directories = getPluginDirectories();

    auto* editor = new JsfxPluginDirectoryEditor(
        directories,
        [this](const juce::StringArray& newDirectories)
        {
            setPluginDirectories(newDirectories);
            refreshPluginList();
        }
    );

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(editor);
    options.dialogTitle = "JSFX Plugin Directories";
    options.resizable = true;
    options.useNativeTitleBar = true;

    auto* window = options.launchAsync();
    if (window != nullptr)
        window->centreWithSize(600, 400);
}

void JsfxPluginWindow::updateButtonsForSelection()
{
    auto selectedItems = pluginTreeView.getSelectedPluginItems();
    bool hasSelection = !selectedItems.isEmpty();

    // Check if any selected item is a plugin (not just a category)
    bool hasPluginSelected = false;
    for (auto* item : selectedItems)
    {
        if (item->getType() == JsfxPluginTreeItem::ItemType::Plugin)
        {
            hasPluginSelected = true;
            break;
        }
    }

    loadButton->setEnabled(hasPluginSelected);
    deleteButton->setEnabled(hasPluginSelected);
}

juce::StringArray JsfxPluginWindow::getPluginDirectories() const
{
    // Load from processor's APVTS
    auto& state = processor.getAPVTS().state;
    auto dirString = state.getProperty("jsfxPluginDirectories", "").toString();

    if (dirString.isEmpty())
    {
        // Return empty array - standard categories will be added automatically
        return juce::StringArray();
    }

    juce::StringArray directories;
    directories.addLines(dirString);
    return directories;
}

void JsfxPluginWindow::setPluginDirectories(const juce::StringArray& directories)
{
    auto& state = processor.getAPVTS().state;
    state.setProperty("jsfxPluginDirectories", directories.joinIntoString("\n"), nullptr);
}

void JsfxPluginWindow::handlePluginTreeItemSelected(juce::TreeViewItem* item)
{
    if (!item)
        return;

    // Cast to JsfxPluginTreeItem to access plugin data
    if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
    {
        // Only load if it's an actual plugin (not a category)
        if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin)
        {
            auto pluginFile = pluginItem->getFile();

            if (processor.loadJSFX(pluginFile))
            {
                getStatusLabel().setText(
                    "Loaded: " + pluginFile.getFileNameWithoutExtension(),
                    juce::dontSendNotification
                );

                // Notify callback if set
                if (onPluginSelected)
                    onPluginSelected(pluginFile.getFullPathName());
            }
            else
            {
                getStatusLabel().setText(
                    "Failed to load: " + pluginFile.getFileNameWithoutExtension(),
                    juce::dontSendNotification
                );
            }
        }
    }
}

//==============================================================================
// JsfxPluginDirectoryEditor Implementation
//==============================================================================

JsfxPluginDirectoryEditor::JsfxPluginDirectoryEditor(
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
        "Enter JSFX plugin search directories (one per line):\n"
        "The plugin browser will scan these directories for .jsfx files.\n\n"
        "Standard categories (User, Local, Remote, REAPER) are always included."
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

JsfxPluginDirectoryEditor::~JsfxPluginDirectoryEditor()
{
    setLookAndFeel(nullptr);
}

void JsfxPluginDirectoryEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void JsfxPluginDirectoryEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    instructionsLabel.setBounds(bounds.removeFromTop(60));
    bounds.removeFromTop(5);

    auto buttonBar = bounds.removeFromBottom(30);
    cancelButton.setBounds(buttonBar.removeFromRight(80));
    buttonBar.removeFromRight(5);
    saveButton.setBounds(buttonBar.removeFromRight(80));
    bounds.removeFromBottom(10);

    directoryEditor.setBounds(bounds);
}

void JsfxPluginDirectoryEditor::saveAndClose()
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

void JsfxPluginDirectoryEditor::cancel()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}
