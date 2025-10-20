#include "JsfxPluginWindow.h"
#include "PluginProcessor.h"
#include "PersistentFileChooser.h"

//==============================================================================
// JsfxPluginWindow Implementation
//==============================================================================

JsfxPluginWindow::JsfxPluginWindow(AudioPluginAudioProcessor& proc)
    : processor(proc)
    , pluginTreeView(proc)
{
    // Set menu title for narrow mode
    setButtonMenuTitle("Plugins");

    // Add buttons to the button row (from base class)
    // Load JSFX File button to open file chooser
    loadJsfxFileButton = &getButtonRow().addButton("Load JSFX File...", [this]() { showJsfxFileChooser(); });
    // Load button reuses the same handler as Enter key / double-click
    loadButton = &getButtonRow().addButton(
        "Load",
        [this]()
        {
            // Use cached item since clicking the button may deselect the tree
            if (cachedSelectedItem)
                handlePluginTreeItemSelected(cachedSelectedItem);
        }
    );
    deleteButton = &getButtonRow().addButton("Delete", [this]() { deleteSelectedPlugins(); });
    directoriesButton = &getButtonRow().addButton("Directories", [this]() { showDirectoryEditor(); });
    repositoriesButton = &getButtonRow().addButton("Repositories", [this]() { showRepositoryEditor(); });
    updateAllButton = &getButtonRow().addButton("Update All", [this]() { updateAllRemotePlugins(); });
    refreshButton = &getButtonRow().addButton("Refresh", [this]() { refreshPluginList(); });

    // Setup tree view
    addAndMakeVisible(pluginTreeView);
    pluginTreeView.onSelectionChangedCallback = [this]() { updateButtonsForSelection(); };

    // Callback when plugin loads (local or remote)
    pluginTreeView.onPluginLoadedCallback = [this](const juce::String& pluginPath, bool success)
    {
        juce::File pluginFile(pluginPath);

        if (success)
        {
            getStatusLabel().setText("Loaded: " + pluginFile.getFileNameWithoutExtension(), juce::dontSendNotification);

            // Notify external callback if set
            if (onPluginSelected)
                onPluginSelected(pluginPath);
        }
        else
        {
            getStatusLabel().setText(
                "Failed to load: " + pluginFile.getFileNameWithoutExtension(),
                juce::dontSendNotification
            );
        }
    };

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

    // Load remote repositories (async)
    pluginTreeView.loadRemoteRepositories();

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
    // Use cached items if available (in case selection was lost when clicking button)
    auto selectedItems = pluginTreeView.getSelectedPluginItems();

    // If no current selection but we have cached item, use that
    if (selectedItems.isEmpty() && cachedSelectedItem)
    {
        if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(cachedSelectedItem))
        {
            if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin)
                selectedItems.add(pluginItem);
        }
    }

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

    juce::String message = "Are you sure you want to move " + juce::String(pluginCount) + " plugin(s) to trash?";

    auto result = juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        "Confirm Delete",
        message,
        "Move to Trash",
        "Cancel",
        nullptr,
        nullptr
    );

    if (result == 0)
        return;

    int deletedCount = 0;

    // Move plugins to trash
    for (auto* item : selectedItems)
    {
        if (item->getType() == JsfxPluginTreeItem::ItemType::Plugin)
        {
            auto pluginFile = item->getFile();
            if (pluginFile.existsAsFile() && pluginFile.moveToTrash())
                deletedCount++;
        }
    }

    getStatusLabel().setText("Moved " + juce::String(deletedCount) + " plugin(s) to trash", juce::dontSendNotification);

    // Clear cache after deleting
    cachedSelectedItem = nullptr;

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

void JsfxPluginWindow::showRepositoryEditor()
{
    auto* editor = new JsfxRepositoryEditor(pluginTreeView, [this]() { refreshPluginList(); });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(editor);
    options.dialogTitle = "Repositories";
    options.resizable = false;
    options.useNativeTitleBar = true;

    auto* window = options.launchAsync();
    if (window != nullptr)
        window->centreWithSize(600, 450);
}

void JsfxPluginWindow::updateAllRemotePlugins()
{
    getStatusLabel().setText("Checking for updates...", juce::dontSendNotification);
    pluginTreeView.updateAllRemotePlugins();
}

void JsfxPluginWindow::updateButtonsForSelection()
{
    auto selectedItems = pluginTreeView.getSelectedPluginItems();

    // Check if any selected item is a plugin (local or remote, not just a category)
    bool hasPluginSelected = false;
    bool hasLocalPluginSelected = false;
    int localPluginCount = 0;

    // Only update cached item if we have a selection (don't clear it on deselection)
    if (!selectedItems.isEmpty())
    {
        cachedSelectedItem = nullptr;

        for (auto* item : selectedItems)
        {
            auto itemType = item->getType();

            if (itemType == JsfxPluginTreeItem::ItemType::Plugin)
            {
                hasPluginSelected = true;
                hasLocalPluginSelected = true;
                localPluginCount++;
                if (!cachedSelectedItem)
                    cachedSelectedItem = item;
            }
            else if (itemType == JsfxPluginTreeItem::ItemType::RemotePlugin)
            {
                hasPluginSelected = true;
                if (!cachedSelectedItem)
                    cachedSelectedItem = item;
            }
        }

        // Update search box placeholder to show selection when not searching
        if (pluginTreeView.getSearchText().isEmpty())
        {
            if (selectedItems.size() == 1 && hasPluginSelected)
            {
                // Show single item name
                if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(selectedItems[0]))
                {
                    pluginTreeView.setSearchPlaceholder(pluginItem->getName());
                    pluginTreeView.repaint(); // Force update to show new placeholder
                }
            }
            else if (selectedItems.size() > 1)
            {
                // Show count
                pluginTreeView.setSearchPlaceholder(juce::String(selectedItems.size()) + " items selected");
                pluginTreeView.repaint(); // Force update to show new placeholder
            }
        }
    }
    else
    {
        // Reset placeholder when nothing selected
        if (pluginTreeView.getSearchText().isEmpty())
        {
            pluginTreeView.setSearchPlaceholder("Type to search...");
            pluginTreeView.repaint(); // Force update to show new placeholder
        }
    }

    // Load button enabled when exactly one plugin is selected OR we have a cached item
    bool shouldEnableLoad = (hasPluginSelected && selectedItems.size() == 1) || cachedSelectedItem != nullptr;
    loadButton->setEnabled(shouldEnableLoad);

    // Delete button enabled when any local Plugin items are selected (not RemotePlugin which are from repos)
    // OR we have a local Plugin cached
    bool hasCachedLocalPlugin =
        cachedSelectedItem
        && dynamic_cast<JsfxPluginTreeItem*>(cachedSelectedItem)
        && dynamic_cast<JsfxPluginTreeItem*>(cachedSelectedItem)->getType() == JsfxPluginTreeItem::ItemType::Plugin;
    bool shouldEnableDelete = hasLocalPluginSelected || hasCachedLocalPlugin;
    deleteButton->setEnabled(shouldEnableDelete);
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
        // Handle local plugins
        if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin)
        {
            auto pluginFile = pluginItem->getFile();
            pluginTreeView.loadPlugin(pluginFile);

            // Clear cache after loading
            cachedSelectedItem = nullptr;
        }
        // Handle remote plugins
        else if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::RemotePlugin)
        {
            auto entry = pluginItem->getReaPackEntry();

            getStatusLabel().setText("Downloading: " + entry.name, juce::dontSendNotification);

            // Download and load via tree view's loadRemotePlugin method
            pluginTreeView.loadRemotePlugin(entry);

            // Clear cache after loading
            cachedSelectedItem = nullptr;

            // Status will be updated via onPluginLoadedCallback
        }
    }
}

void JsfxPluginWindow::showJsfxFileChooser()
{
    // Use PersistentFileChooser for consistent directory management
    // Only show files without extension or with .jsfx extension
    auto localFileChooser =
        std::make_unique<PersistentFileChooser>("lastJsfxDirectory", "Select a JSFX file to load...", "*.jsfx;*.");

    localFileChooser->launchAsync(
        [this](const juce::File& file)
        {
            if (file != juce::File{})
            {
                // Load the plugin through the tree view's loadPlugin method
                pluginTreeView.loadPlugin(file);
            }
        }
    );

    // Keep the file chooser alive by storing it as a member (it will auto-delete when done)
    fileChooser = std::move(localFileChooser);
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

    // Remove empty lines and trim whitespace/quotes from each line
    directories.removeEmptyStrings();

    for (int i = 0; i < directories.size(); ++i)
    {
        auto dir = directories[i].trim();
        // Remove surrounding quotes if present
        if (dir.startsWith("\"") && dir.endsWith("\""))
            dir = dir.substring(1, dir.length() - 1);
        directories.set(i, dir);
    }

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

//==============================================================================
// JsfxRepositoryEditor Implementation
//==============================================================================

JsfxRepositoryEditor::JsfxRepositoryEditor(JsfxPluginTreeView& treeView, std::function<void()> onSave)
    : pluginTreeView(treeView)
    , saveCallback(onSave)
{
    setLookAndFeel(&sharedLookAndFeel->lf);

    addAndMakeVisible(instructionsLabel);
    instructionsLabel.setMultiLine(true);
    instructionsLabel.setReadOnly(true);
    instructionsLabel.setScrollbarsShown(false);
    instructionsLabel.setCaretVisible(false);
    instructionsLabel.setPopupMenuEnabled(false);
    instructionsLabel.setText(
        "Manage remote ReaPack-compatible JSFX repositories.\n"
        "Enter a repository URL to fetch its information."
    );
    instructionsLabel.setFont(juce::FontOptions(12.0f));
    instructionsLabel.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    instructionsLabel.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);

    // Load current repositories
    auto repos = pluginTreeView.getRemoteRepositories();
    for (const auto& [name, url] : repos)
        listModel.repositories.push_back({name, url});

    addAndMakeVisible(repositoryList);
    repositoryList.setModel(&listModel);
    repositoryList.setMultipleSelectionEnabled(false);
    repositoryList.selectRow(-1);

    // Update buttons when selection changes
    repositoryList.addMouseListener(this, true);
    auto updateSelection = [this]() { updateButtonStates(); };

    addAndMakeVisible(urlEditor);
    urlEditor.setTextToShowWhenEmpty("https://example.com/index.xml", juce::Colours::grey);
    urlEditor.onTextChange = [this]() { onUrlChanged(); };
    urlEditor.onReturnKey = [this]() { fetchRepositoryName(); };
    urlEditor.onFocusLost = [this]() { fetchRepositoryName(); };

    addAndMakeVisible(nameEditor);
    nameEditor.setTextToShowWhenEmpty("Repository Name", juce::Colours::darkgrey);
    nameEditor.setEnabled(false);
    nameEditor.onReturnKey = [this]()
    {
        if (addButton.isEnabled())
            addRepository();
    };
    nameEditor.onFocusLost = [this]() { fetchRepositoryName(); };

    addAndMakeVisible(addButton);
    addButton.onClick = [this]() { addRepository(); };
    addButton.setEnabled(false);

    addAndMakeVisible(removeButton);
    removeButton.onClick = [this]() { removeSelectedRepository(); };
    removeButton.setEnabled(false);

    addAndMakeVisible(saveButton);
    saveButton.onClick = [this]() { saveAndClose(); };

    addAndMakeVisible(cancelButton);
    cancelButton.onClick = [this]() { cancel(); };

    setSize(600, 450);
}

JsfxRepositoryEditor::~JsfxRepositoryEditor()
{
    repositoryList.setModel(nullptr);
    setLookAndFeel(nullptr);
}

void JsfxRepositoryEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void JsfxRepositoryEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    instructionsLabel.setBounds(bounds.removeFromTop(35));
    bounds.removeFromTop(5);

    // Repository list with add/remove buttons on the right
    auto listArea = bounds.removeFromTop(200);
    auto listButtons = listArea.removeFromRight(80);
    listButtons.removeFromLeft(5);

    removeButton.setBounds(listButtons.removeFromTop(30));

    listArea.removeFromRight(5);
    repositoryList.setBounds(listArea);

    bounds.removeFromTop(10);

    // Add repository section
    auto urlLabel = bounds.removeFromTop(15);
    urlLabel.removeFromBottom(2);
    // Could add a label here if needed

    auto urlRow = bounds.removeFromTop(25);
    urlEditor.setBounds(urlRow);
    bounds.removeFromTop(5);

    auto nameRow = bounds.removeFromTop(25);
    auto nameRowRight = nameRow.removeFromRight(80);
    nameRowRight.removeFromLeft(5);
    addButton.setBounds(nameRowRight);
    nameRow.removeFromRight(5);
    nameEditor.setBounds(nameRow);

    bounds.removeFromTop(10);

    // Bottom buttons
    auto buttonBar = bounds.removeFromBottom(30);
    cancelButton.setBounds(buttonBar.removeFromRight(80));
    buttonBar.removeFromRight(5);
    saveButton.setBounds(buttonBar.removeFromRight(80));
}

void JsfxRepositoryEditor::onUrlChanged()
{
    auto url = urlEditor.getText().trim();

    // Validate URL format
    if (url.isEmpty())
    {
        nameEditor.clear();
        nameEditor.setEnabled(false);
        addButton.setEnabled(false);
        return;
    }

    // Check if it looks like a valid URL
    if (!url.startsWith("http://") && !url.startsWith("https://"))
    {
        nameEditor.clear();
        nameEditor.setEnabled(false);
        addButton.setEnabled(false);
        return;
    }
}

void JsfxRepositoryEditor::fetchRepositoryName()
{
    auto url = urlEditor.getText().trim();

    // Only fetch if URL is valid
    if (url.isEmpty() || (!url.startsWith("http://") && !url.startsWith("https://")))
        return;

    // Clear and disable while fetching
    nameEditor.clear();
    nameEditor.setEnabled(false);
    addButton.setEnabled(false);
    nameEditor.setTextToShowWhenEmpty("Fetching...", juce::Colours::darkgrey);

    // Download and parse index in background
    juce::Thread::launch(
        [url, this]()
        {
            auto inputStream = juce::URL(url).createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(5000)
            );

            juce::String repoName;
            bool isValidReaPack = false;

            if (inputStream != nullptr)
            {
                juce::String xmlContent = inputStream->readEntireStreamAsString();
                repoName = ReaPackIndexParser::getRepositoryName(xmlContent);

                // Only consider valid if we successfully parsed a ReaPack index
                if (repoName.isNotEmpty())
                    isValidReaPack = true;
            }

            // Update name on message thread
            juce::MessageManager::callAsync(
                [this, repoName, isValidReaPack]()
                {
                    nameEditor.setTextToShowWhenEmpty("Repository Name", juce::Colours::darkgrey);

                    if (isValidReaPack)
                    {
                        nameEditor.setText(repoName);
                        nameEditor.setEnabled(true);
                        addButton.setEnabled(true);
                    }
                    else
                    {
                        nameEditor.clear();
                        nameEditor.setEnabled(false);
                        addButton.setEnabled(false);

                        // Show error message
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::WarningIcon,
                            "Invalid Repository",
                            "The URL does not point to a valid ReaPack index file."
                        );
                    }
                }
            );
        }
    );
}

void JsfxRepositoryEditor::updateButtonStates()
{
    bool hasSelection = repositoryList.getSelectedRow() >= 0;
    removeButton.setEnabled(hasSelection);
}

void JsfxRepositoryEditor::addRepository()
{
    auto url = urlEditor.getText().trim();
    auto name = nameEditor.getText().trim();

    if (url.isEmpty() || name.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Invalid Input",
            "Please enter a valid repository URL and wait for the name to be fetched."
        );
        return;
    }

    // Check for duplicates
    for (const auto& repo : listModel.repositories)
    {
        if (repo.name == name || repo.url == url)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Duplicate Entry",
                "A repository with this name or URL already exists."
            );
            return;
        }
    }

    listModel.repositories.push_back({name, url});
    repositoryList.updateContent();

    // Clear input fields and reset state
    nameEditor.clear();
    urlEditor.clear();
    nameEditor.setEnabled(false);
    addButton.setEnabled(false);
}

void JsfxRepositoryEditor::removeSelectedRepository()
{
    int selectedRow = repositoryList.getSelectedRow();
    if (selectedRow >= 0 && selectedRow < (int)listModel.repositories.size())
    {
        listModel.repositories.erase(listModel.repositories.begin() + selectedRow);
        repositoryList.updateContent();
    }
}

void JsfxRepositoryEditor::saveAndClose()
{
    // Convert to vector of pairs
    std::vector<std::pair<juce::String, juce::String>> repos;
    for (const auto& repo : listModel.repositories)
        repos.push_back({repo.name, repo.url});

    pluginTreeView.setRemoteRepositories(repos);

    if (saveCallback)
        saveCallback();

    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(1);
}

void JsfxRepositoryEditor::cancel()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}
