#include "PresetBrowser.h"
#include "PluginProcessor.h"
#include "PresetConverter.h"
#include "ReaperPresetConverter.h"

// PresetTreeItem implementation
PresetTreeItem::PresetTreeItem(const juce::String& name, ItemType itemType, const juce::String& path)
    : itemName(name)
    , type(itemType)
    , filePath(path)
{
}

bool PresetTreeItem::mightContainSubItems()
{
    return type == ItemType::Root || type == ItemType::File || type == ItemType::Bank;
}

void PresetTreeItem::paintItem(juce::Graphics& g, int width, int height)
{
    // Get colours from the tree view's look and feel
    auto* ownerView = getOwnerView();
    if (ownerView == nullptr)
        return;

    auto& lf = ownerView->getLookAndFeel();

    // Draw selection highlight with a more visible color
    if (isSelected())
    {
        g.setColour(juce::Colours::blue.withAlpha(0.4f));
        g.fillAll();
    }

    // Use default text colour from look and feel
    g.setColour(lf.findColour(juce::TreeView::backgroundColourId).contrasting());
    g.setFont(juce::FontOptions(14.0f));

    juce::String displayText = itemName;
    if (type == ItemType::File)
        displayText += " (File)";
    else if (type == ItemType::Bank)
        displayText += " (Bank)";

    g.drawText(displayText, 4, 0, width - 4, height, juce::Justification::centredLeft, true);
}

// PresetBrowserWindow implementation
PresetBrowserWindow::PresetBrowserWindow(PresetManager& presetMgr, const juce::String& currentJsfxPath)
    : presetManager(presetMgr)
    , jsfxPath(currentJsfxPath)
{
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Preset Manager", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(18.0f).withStyle("Bold"));
    titleLabel.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(presetTree);
    presetTree.setMultiSelectEnabled(true);

    addAndMakeVisible(importButton);
    importButton.setButtonText("Import...");
    importButton.onClick = [this]() { importPresets(); };

    addAndMakeVisible(exportButton);
    exportButton.setButtonText("Export Selected...");
    exportButton.onClick = [this]() { exportSelected(); };

    addAndMakeVisible(deleteButton);
    deleteButton.setButtonText("Delete Selected");
    deleteButton.onClick = [this]() { deleteSelected(); };

    setSize(500, 600);

    refreshPresetTree();
}

PresetBrowserWindow::~PresetBrowserWindow()
{
    // Clear the tree view's root item before destruction to avoid dangling pointer
    presetTree.setRootItem(nullptr);
}

void PresetBrowserWindow::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void PresetBrowserWindow::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    // Title
    titleLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(10);

    // Buttons at bottom
    auto buttonBar = bounds.removeFromBottom(30);
    importButton.setBounds(buttonBar.removeFromLeft(100));
    buttonBar.removeFromLeft(5);
    exportButton.setBounds(buttonBar.removeFromLeft(150));
    buttonBar.removeFromLeft(5);
    deleteButton.setBounds(buttonBar.removeFromLeft(120));
    bounds.removeFromBottom(10);

    // Tree view fills remaining space
    presetTree.setBounds(bounds);
}

void PresetBrowserWindow::refreshPresetTree()
{
    // Clear existing tree first to avoid dangling pointers
    presetTree.setRootItem(nullptr);

    // Create root item showing JSFX name
    auto jsfxFile = juce::File(jsfxPath);
    auto jsfxName = jsfxFile.getFileNameWithoutExtension();
    rootItem = std::make_unique<PresetTreeItem>(jsfxName, PresetTreeItem::ItemType::Root);

    // Get storage directory for current JSFX
    auto storageDir = presetManager.getJsfxStorageDirectory();

    if (!storageDir.exists())
    {
        presetTree.setRootItem(rootItem.get());
        return;
    }

    // Scan for .rpl files in storage directory
    auto presetFiles = storageDir.findChildFiles(juce::File::findFiles, false, "*.rpl");

    ReaperPresetConverter converter;

    for (const auto& file : presetFiles)
    {
        // Create file item (preset-filename level)
        auto* fileItem = new PresetTreeItem(
            file.getFileNameWithoutExtension(),
            PresetTreeItem::ItemType::File,
            file.getFullPathName()
        );
        rootItem->addSubItem(fileItem);

        // Parse the .rpl file to extract banks and presets
        auto presetTree = converter.convertFileToTree(file);

        if (presetTree.isValid())
        {
            // Iterate through banks in this file
            for (int i = 0; i < presetTree.getNumChildren(); ++i)
            {
                auto bank = presetTree.getChild(i);
                if (bank.getType().toString() == "PresetBank")
                {
                    auto bankName = bank.getProperty("name").toString();
                    auto* bankItem = new PresetTreeItem(bankName, PresetTreeItem::ItemType::Bank);
                    fileItem->addSubItem(bankItem);

                    // Iterate through presets in this bank
                    for (int j = 0; j < bank.getNumChildren(); ++j)
                    {
                        auto preset = bank.getChild(j);
                        if (preset.getType().toString() == "Preset")
                        {
                            auto presetName = preset.getProperty("name").toString();
                            auto* presetItem = new PresetTreeItem(presetName, PresetTreeItem::ItemType::Preset);
                            bankItem->addSubItem(presetItem);
                        }
                    }
                }
            }
        }
    }

    // Set the new root item after it's fully populated
    presetTree.setRootItem(rootItem.get());
    rootItem->setOpen(true);
}

void PresetBrowserWindow::importPresets()
{
    presetManager.importPreset(this);
    refreshPresetTree();
}

void PresetBrowserWindow::collectSelectedItems(juce::Array<PresetTreeItem*>& items, PresetTreeItem* item)
{
    if (item == nullptr)
        return;

    if (item->isSelected())
        items.add(item);

    for (int i = 0; i < item->getNumSubItems(); ++i)
        if (auto* subItem = dynamic_cast<PresetTreeItem*>(item->getSubItem(i)))
            collectSelectedItems(items, subItem);
}

juce::Array<PresetTreeItem*> PresetBrowserWindow::getSelectedItems()
{
    juce::Array<PresetTreeItem*> items;
    if (rootItem)
        collectSelectedItems(items, rootItem.get());
    return items;
}

void PresetBrowserWindow::exportSelected()
{
    auto selectedItems = getSelectedItems();

    if (selectedItems.isEmpty())
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Export",
            "Please select one or more items to export.",
            this,
            nullptr
        );
        return;
    }

    // Create a file chooser for export destination
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Presets",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        "*.rpl"
    );

    auto chooserFlags = juce::FileBrowserComponent::saveMode
                      | juce::FileBrowserComponent::canSelectFiles
                      | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync(
        chooserFlags,
        [this, selectedItems, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            DBG("Export callback triggered");
            DBG("Selected file: " << file.getFullPathName());

            // Check if user cancelled
            if (file == juce::File())
            {
                DBG("User cancelled export");
                return;
            }

            // Ensure .rpl extension
            if (file.getFileExtension() != ".rpl")
                file = file.withFileExtension(".rpl");

            DBG("Export file with extension: " << file.getFullPathName());
            DBG("Number of selected items: " << selectedItems.size());

            // Build a ValueTree with selected items
            juce::ValueTree exportTree("PresetFile");
            exportTree.setProperty("name", file.getFileNameWithoutExtension(), nullptr);
            exportTree.setProperty("file", file.getFullPathName(), nullptr);

            ReaperPresetConverter converter;

            // Collect all selected presets organized by bank
            juce::HashMap<juce::String, juce::ValueTree> banks;

            for (auto* item : selectedItems)
            {
                auto itemType = item->getType();

                if (itemType == PresetTreeItem::ItemType::Preset)
                {
                    // Find parent bank and file to read preset data
                    auto* bankItem = dynamic_cast<PresetTreeItem*>(item->getParentItem());
                    auto* fileItem = bankItem ? dynamic_cast<PresetTreeItem*>(bankItem->getParentItem()) : nullptr;

                    if (fileItem && bankItem)
                    {
                        juce::String bankName = bankItem->getName();
                        juce::File sourceFile(fileItem->getFilePath());

                        // Parse source file to get preset data
                        auto sourceTree = converter.convertFileToTree(sourceFile);

                        // Find the bank in source tree
                        for (int i = 0; i < sourceTree.getNumChildren(); ++i)
                        {
                            auto bank = sourceTree.getChild(i);
                            if (bank.getProperty("name").toString() == bankName)
                            {
                                // Find the preset in the bank
                                for (int j = 0; j < bank.getNumChildren(); ++j)
                                {
                                    auto preset = bank.getChild(j);
                                    if (preset.getProperty("name").toString() == item->getName())
                                    {
                                        // Add or get bank in export tree
                                        if (!banks.contains(bankName))
                                        {
                                            juce::ValueTree newBank("PresetBank");
                                            newBank.setProperty("name", bankName, nullptr);
                                            banks.set(bankName, newBank);
                                            exportTree.appendChild(newBank, nullptr);
                                        }

                                        // Add preset to bank
                                        banks[bankName].appendChild(preset.createCopy(), nullptr);
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
                else if (itemType == PresetTreeItem::ItemType::Bank)
                {
                    // Export entire bank
                    auto* fileItem = dynamic_cast<PresetTreeItem*>(item->getParentItem());
                    if (fileItem)
                    {
                        juce::String bankName = item->getName();
                        juce::File sourceFile(fileItem->getFilePath());

                        auto sourceTree = converter.convertFileToTree(sourceFile);

                        for (int i = 0; i < sourceTree.getNumChildren(); ++i)
                        {
                            auto bank = sourceTree.getChild(i);
                            if (bank.getProperty("name").toString() == bankName)
                            {
                                exportTree.appendChild(bank.createCopy(), nullptr);
                                break;
                            }
                        }
                    }
                }
                else if (itemType == PresetTreeItem::ItemType::File)
                {
                    // Export entire file
                    juce::File sourceFile(item->getFilePath());
                    auto sourceTree = converter.convertFileToTree(sourceFile);

                    for (int i = 0; i < sourceTree.getNumChildren(); ++i)
                        exportTree.appendChild(sourceTree.getChild(i).createCopy(), nullptr);
                }
            }

            // Write to file
            DBG("About to write export tree with " << exportTree.getNumChildren() << " banks");

            if (converter.convertTreeToFile(exportTree, file))
            {
                DBG("Export successful");
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    "Export",
                    "Presets exported successfully to:\n" + file.getFullPathName(),
                    this,
                    nullptr
                );
            }
            else
            {
                DBG("Export failed");
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Export Failed",
                    "Failed to export presets to:\n" + file.getFullPathName(),
                    this,
                    nullptr
                );
            }
        }
    );
}

void PresetBrowserWindow::deleteSelected()
{
    auto selectedItems = getSelectedItems();

    if (selectedItems.isEmpty())
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Delete",
            "Please select one or more items to delete.",
            this,
            nullptr
        );
        return;
    }

    // Build description of what will be deleted
    juce::String message = "Are you sure you want to delete the following items?\n\n";
    int presetCount = 0, bankCount = 0, fileCount = 0;

    for (auto* item : selectedItems)
    {
        auto itemType = item->getType();
        if (itemType == PresetTreeItem::ItemType::Preset)
            presetCount++;
        else if (itemType == PresetTreeItem::ItemType::Bank)
            bankCount++;
        else if (itemType == PresetTreeItem::ItemType::File)
            fileCount++;
    }

    if (fileCount > 0)
        message += juce::String(fileCount) + " file(s)\n";
    if (bankCount > 0)
        message += juce::String(bankCount) + " bank(s)\n";
    if (presetCount > 0)
        message += juce::String(presetCount) + " preset(s)\n";

    message += "\nThis action cannot be undone.";

    // Show confirmation dialog
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::WarningIcon)
                       .withTitle("Confirm Delete")
                       .withMessage(message)
                       .withButton("OK")
                       .withButton("Cancel")
                       .withParentComponent(this);

    juce::AlertWindow::showAsync(
        options,
        [this, selectedItems](int result)
        {
            if (result != 1) // Cancel was pressed
                return;

            ReaperPresetConverter converter;
            juce::HashMap<juce::String, bool> filesToDelete;            // Track files to completely delete
            juce::HashMap<juce::String, juce::ValueTree> filesToUpdate; // Track files to update

            // Process deletions
            for (auto* item : selectedItems)
            {
                auto itemType = item->getType();

                if (itemType == PresetTreeItem::ItemType::File)
                {
                    // Mark entire file for deletion
                    filesToDelete.set(item->getFilePath(), true);
                }
                else if (itemType == PresetTreeItem::ItemType::Bank || itemType == PresetTreeItem::ItemType::Preset)
                {
                    // Need to update the file
                    auto* fileItem = item;

                    // Navigate up to find the file item
                    while (fileItem && fileItem->getType() != PresetTreeItem::ItemType::File)
                        fileItem = dynamic_cast<PresetTreeItem*>(fileItem->getParentItem());

                    if (fileItem)
                    {
                        juce::String filePath = fileItem->getFilePath();

                        // Don't process if file is marked for complete deletion
                        if (filesToDelete.contains(filePath))
                            continue;

                        // Load file tree if not already loaded
                        if (!filesToUpdate.contains(filePath))
                        {
                            juce::File file(filePath);
                            auto tree = converter.convertFileToTree(file);
                            filesToUpdate.set(filePath, tree);
                        }

                        auto& fileTree = filesToUpdate.getReference(filePath);

                        if (itemType == PresetTreeItem::ItemType::Bank)
                        {
                            // Remove entire bank
                            juce::String bankName = item->getName();
                            for (int i = 0; i < fileTree.getNumChildren(); ++i)
                            {
                                auto bank = fileTree.getChild(i);
                                if (bank.getProperty("name").toString() == bankName)
                                {
                                    fileTree.removeChild(i, nullptr);
                                    break;
                                }
                            }
                        }
                        else if (itemType == PresetTreeItem::ItemType::Preset)
                        {
                            // Remove single preset
                            auto* bankItem = dynamic_cast<PresetTreeItem*>(item->getParentItem());
                            if (bankItem)
                            {
                                juce::String bankName = bankItem->getName();
                                juce::String presetName = item->getName();

                                for (int i = 0; i < fileTree.getNumChildren(); ++i)
                                {
                                    auto bank = fileTree.getChild(i);
                                    if (bank.getProperty("name").toString() == bankName)
                                    {
                                        for (int j = 0; j < bank.getNumChildren(); ++j)
                                        {
                                            auto preset = bank.getChild(j);
                                            if (preset.getProperty("name").toString() == presetName)
                                            {
                                                bank.removeChild(j, nullptr);
                                                break;
                                            }
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Delete entire files
            for (auto it = filesToDelete.begin(); it != filesToDelete.end(); ++it)
            {
                juce::File file(it.getKey());
                file.deleteFile();
            }

            // Update modified files
            for (auto it = filesToUpdate.begin(); it != filesToUpdate.end(); ++it)
            {
                juce::File file(it.getKey());
                auto tree = it.getValue();

                // If no banks left, delete the file
                if (tree.getNumChildren() == 0)
                {
                    file.deleteFile();
                }
                else
                {
                    // Write updated tree back to file
                    converter.convertTreeToFile(tree, file);
                }
            }

            // Refresh the tree view
            refreshPresetTree();

            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Delete",
                "Selected items deleted successfully.",
                this,
                nullptr
            );
        }
    );
}
