#include "PresetTreeView.h"
#include "PluginProcessor.h"

//==============================================================================
// PresetTreeItem Implementation
//==============================================================================

PresetTreeItem::PresetTreeItem(
    const juce::String& name,
    ItemType t,
    const juce::File& file,
    const juce::String& bankName,
    const juce::String& presetName,
    const juce::String& presetData,
    PresetTreeView* view
)
    : itemName(name)
    , type(t)
    , presetFile(file)
    , bank(bankName)
    , preset(presetName)
    , data(presetData)
    , presetTreeView(view)
{
}

bool PresetTreeItem::mightContainSubItems()
{
    return type != ItemType::Preset; // Only Preset items are leaves
}

bool PresetTreeItem::canBeSelected() const
{
    return true; // All items can be selected
}

void PresetTreeItem::itemDoubleClicked(const juce::MouseEvent&)
{
    // Execute command (same as Enter key) - this will trigger onCommand callback
    if (type == ItemType::Preset && presetTreeView && !data.isEmpty())
    {
        juce::Array<juce::TreeViewItem*> items;
        items.add(this);
        presetTreeView->executeCommand(items);
    }
}

void PresetTreeItem::itemSelectionChanged(bool isNowSelected)
{
    repaintItem();

    // Notify the tree view of selection change
    if (presetTreeView)
        presetTreeView->onSelectionChanged();
}

//==============================================================================
// PresetTreeView Implementation
//==============================================================================

PresetTreeView::PresetTreeView(AudioPluginAudioProcessor& proc)
    : processor(proc)
{
}

PresetTreeView::~PresetTreeView() = default;

void PresetTreeView::loadPresets(const juce::StringArray& directoryPaths)
{
    presetDirectories.clear();

    // Determine standard directories
    auto reaperDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("REAPER")
                         .getChildFile("Effects");

    // Get current JSFX plugin directory
    juce::String jsfxPath = processor.getCurrentJSFXPath();
    juce::File currentJsfxDir;
    if (jsfxPath.isNotEmpty())
        currentJsfxDir = juce::File(jsfxPath).getParentDirectory();

    for (const auto& path : directoryPaths)
    {
        juce::File dir(path);
        if (!dir.exists() || !dir.isDirectory())
            continue;

        DirectoryEntry dirEntry;
        dirEntry.directory = dir;

        // Determine directory type and display name
        auto dirPath = dir.getFullPathName();

        if (currentJsfxDir.exists() && dir == currentJsfxDir)
        {
            dirEntry.isCurrentJsfxDir = true;
            dirEntry.displayName = "Current JSFX";
        }
        else if (dir.isAChildOf(reaperDir) || dirPath.startsWith(reaperDir.getFullPathName()))
        {
            dirEntry.isReaperDir = true;
            dirEntry.displayName = "REAPER";
        }
        else
        {
            dirEntry.isCustomDir = true;
            dirEntry.displayName = dir.getFileName();
        }

        // Scan for .rpl files (recursively for REAPER)
        bool scanRecursively = dirEntry.isReaperDir;
        auto files = dir.findChildFiles(juce::File::findFiles, scanRecursively, "*.rpl");

        for (const auto& file : files)
        {
            FileEntry fileEntry;
            fileEntry.file = file;
            parsePresetFile(file, fileEntry);

            if (!fileEntry.banks.empty())
                dirEntry.files.push_back(fileEntry);
        }

        if (!dirEntry.files.empty())
            presetDirectories.push_back(dirEntry);
    }

    // Sort directories: current JSFX first, then custom, then REAPER
    std::sort(
        presetDirectories.begin(),
        presetDirectories.end(),
        [](const DirectoryEntry& a, const DirectoryEntry& b)
        {
            if (a.isCurrentJsfxDir != b.isCurrentJsfxDir)
                return a.isCurrentJsfxDir;
            if (a.isCustomDir != b.isCustomDir)
                return a.isCustomDir;
            if (a.isReaperDir != b.isReaperDir)
                return a.isReaperDir;
            return false;
        }
    );

    refreshTree();
}

void PresetTreeView::loadPresetsFromValueTree(const juce::ValueTree& presetsNode)
{
    presetDirectories.clear();

    if (!presetsNode.isValid())
    {
        refreshTree();
        return;
    }

    // Determine standard directory paths
    auto reaperDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("REAPER")
                         .getChildFile("Effects");

    // Get current JSFX plugin directory
    juce::String jsfxPath = processor.getCurrentJSFXPath();
    juce::File currentJsfxDir;
    if (jsfxPath.isNotEmpty())
        currentJsfxDir = juce::File(jsfxPath).getParentDirectory();

    // Group files by their scan root directory
    std::map<juce::File, std::vector<FileEntry>> filesByScanRoot;

    // Convert ValueTree structure to our internal format
    for (int fileIdx = 0; fileIdx < presetsNode.getNumChildren(); ++fileIdx)
    {
        auto fileNode = presetsNode.getChild(fileIdx);
        if (!fileNode.isValid())
            continue;

        FileEntry fileEntry;

        // Get file path from ValueTree
        auto filePath = fileNode.getProperty("file", "").toString();
        if (filePath.isEmpty())
            continue; // Skip files without paths

        fileEntry.file = juce::File(filePath);

        // Convert banks
        for (int bankIdx = 0; bankIdx < fileNode.getNumChildren(); ++bankIdx)
        {
            auto bankNode = fileNode.getChild(bankIdx);
            if (!bankNode.isValid())
                continue;

            BankEntry bankEntry;
            bankEntry.bankName = bankNode.getProperty("name", "Unknown Bank").toString();

            // Convert presets
            for (int presetIdx = 0; presetIdx < bankNode.getNumChildren(); ++presetIdx)
            {
                auto presetNode = bankNode.getChild(presetIdx);
                if (!presetNode.isValid())
                    continue;

                PresetEntry presetEntry;
                presetEntry.file = fileEntry.file;
                presetEntry.bank = bankEntry.bankName;
                presetEntry.preset = presetNode.getProperty("name", "Unknown Preset").toString();
                presetEntry.data = presetNode.getProperty("data", "").toString();

                bankEntry.presets.push_back(presetEntry);
            }

            if (!bankEntry.presets.empty())
                fileEntry.banks.push_back(bankEntry);
        }

        if (!fileEntry.banks.empty())
        {
            // Group by parent directory (the JSFX folder containing the .rpl file)
            juce::File scanRoot = fileEntry.file.getParentDirectory();
            filesByScanRoot[scanRoot].push_back(fileEntry);
        }
    }

    // Create directory entries for each scan root with proper categorization
    for (const auto& pair : filesByScanRoot)
    {
        DirectoryEntry dirEntry;
        dirEntry.directory = pair.first;
        dirEntry.files = pair.second;

        // Determine directory type and display name
        auto dirPath = pair.first.getFullPathName();

        if (currentJsfxDir.exists() && pair.first == currentJsfxDir)
        {
            // Current JSFX plugin directory
            dirEntry.isCurrentJsfxDir = true;
            dirEntry.displayName = "Current JSFX";
        }
        else if (pair.first.isAChildOf(reaperDir) || dirPath.startsWith(reaperDir.getFullPathName()))
        {
            // REAPER directory
            dirEntry.isReaperDir = true;
            dirEntry.displayName = "REAPER";
        }
        else
        {
            // Custom directory
            dirEntry.isCustomDir = true;
            dirEntry.displayName = pair.first.getFileName();
        }

        presetDirectories.push_back(dirEntry);
    }

    // Sort directories: current JSFX first, then custom, then REAPER
    std::sort(
        presetDirectories.begin(),
        presetDirectories.end(),
        [](const DirectoryEntry& a, const DirectoryEntry& b)
        {
            if (a.isCurrentJsfxDir != b.isCurrentJsfxDir)
                return a.isCurrentJsfxDir; // Current JSFX first
            if (a.isCustomDir != b.isCustomDir)
                return a.isCustomDir; // Custom second
            if (a.isReaperDir != b.isReaperDir)
                return a.isReaperDir; // REAPER third
            return false;             // Maintain order
        }
    );

    refreshTree();
}

void PresetTreeView::parsePresetFile(const juce::File& file, FileEntry& fileEntry)
{
    auto content = file.loadFileAsString();
    if (content.isEmpty())
        return;

    const char* data = content.toRawUTF8();
    int len = content.length();
    int pos = 0;

    // Find all <REAPER_PRESET_LIBRARY> blocks
    while (pos < len)
    {
        int libStart = content.indexOf(pos, "<REAPER_PRESET_LIBRARY");
        if (libStart == -1)
            break;

        // Extract library/bank name
        int nameStart = libStart + 22;
        while (
            nameStart < len
            && (data[nameStart] == ' ' || data[nameStart] == '\t' || data[nameStart] == '\r' || data[nameStart] == '\n')
        )
            nameStart++;

        if (nameStart >= len)
            break;

        char quoteChar = data[nameStart];
        int nameEnd = nameStart + 1;
        while (nameEnd < len && data[nameEnd] != quoteChar)
            nameEnd++;

        if (nameEnd >= len)
            break;

        juce::String bankName = content.substring(nameStart + 1, nameEnd);

        // Remove "JS: " prefix if present
        if (bankName.startsWith("JS: "))
            bankName = bankName.substring(4);

        // Find closing > for the opening tag first
        int openTagEnd = nameEnd + 1;
        while (openTagEnd < len && data[openTagEnd] != '>')
            openTagEnd++;

        if (openTagEnd >= len)
            break;

        // Find closing > for library (now we're inside the tag)
        int depth = 1;
        int libraryEnd = -1;
        for (int i = openTagEnd + 1; i < len && depth > 0; i++)
        {
            char c = data[i];
            if (c == '`' || c == '"' || c == '\'')
            {
                char quote = c;
                i++;
                while (i < len && data[i] != quote)
                    i++;
                continue;
            }
            if (c == '<')
                depth++;
            else if (c == '>')
            {
                depth--;
                if (depth == 0)
                {
                    libraryEnd = i;
                    break;
                }
            }
        }

        if (libraryEnd == -1)
            break;

        BankEntry bankEntry;
        bankEntry.bankName = bankName;

        // Parse presets in this bank
        int presetPos = openTagEnd + 1;
        while (presetPos < libraryEnd)
        {
            int presetStart = presetPos;
            while (presetStart < libraryEnd
                   && (presetStart + 7 >= libraryEnd || strncmp(data + presetStart, "<PRESET", 7) != 0))
                presetStart++;

            if (presetStart >= libraryEnd)
                break;

            // Find preset name
            int pNameStart = presetStart + 7;
            while (pNameStart < libraryEnd
                   && (data[pNameStart] == ' '
                       || data[pNameStart] == '\t'
                       || data[pNameStart] == '\r'
                       || data[pNameStart] == '\n'))
                pNameStart++;

            if (pNameStart >= libraryEnd)
                break;

            char pQuoteChar = data[pNameStart];
            int pNameEnd = pNameStart + 1;
            while (pNameEnd < libraryEnd && data[pNameEnd] != pQuoteChar)
                pNameEnd++;

            if (pNameEnd >= libraryEnd)
                break;

            juce::String presetName = content.substring(pNameStart + 1, pNameEnd);

            // Find closing > for preset
            int pDepth = 1;
            int presetEnd = -1;
            for (int i = pNameEnd + 1; i < libraryEnd && pDepth > 0; i++)
            {
                char c = data[i];
                if (c == '`' || c == '"' || c == '\'')
                {
                    char quote = c;
                    i++;
                    while (i < libraryEnd && data[i] != quote)
                        i++;
                    continue;
                }
                if (c == '<')
                    pDepth++;
                else if (c == '>')
                {
                    pDepth--;
                    if (pDepth == 0)
                    {
                        presetEnd = i;
                        break;
                    }
                }
            }

            if (presetEnd == -1)
            {
                presetPos = presetStart + 1;
                continue;
            }

            // Extract preset data (base64)
            juce::String presetData = content.substring(pNameEnd + 1, presetEnd).trim();

            PresetEntry presetEntry;
            presetEntry.file = file;
            presetEntry.bank = bankName;
            presetEntry.preset = presetName;
            presetEntry.data = presetData;

            bankEntry.presets.push_back(presetEntry);

            presetPos = presetEnd + 1;
        }

        if (!bankEntry.presets.empty())
            fileEntry.banks.push_back(bankEntry);

        pos = libraryEnd + 1;
    }
}

std::unique_ptr<juce::TreeViewItem> PresetTreeView::createRootItem()
{
    auto root = std::make_unique<PresetTreeItem>("Root", PresetTreeItem::ItemType::Directory);

    // Helper to add file tree (file -> banks -> presets) to a parent item
    auto addFileTree = [this](PresetTreeItem* parent, const FileEntry& fileEntry)
    {
        auto fileItem = std::make_unique<PresetTreeItem>(
            fileEntry.file.getFileName(),
            PresetTreeItem::ItemType::File,
            fileEntry.file,
            juce::String(),
            juce::String(),
            juce::String(),
            this
        );

        for (const auto& bankEntry : fileEntry.banks)
        {
            auto bankItem = std::make_unique<PresetTreeItem>(
                bankEntry.bankName,
                PresetTreeItem::ItemType::Bank,
                fileEntry.file,
                bankEntry.bankName,
                juce::String(),
                juce::String(),
                this
            );

            for (const auto& presetEntry : bankEntry.presets)
            {
                auto presetItem = std::make_unique<PresetTreeItem>(
                    presetEntry.preset,
                    PresetTreeItem::ItemType::Preset,
                    presetEntry.file,
                    presetEntry.bank,
                    presetEntry.preset,
                    presetEntry.data,
                    this
                );
                bankItem->addSubItem(presetItem.release());
            }
            fileItem->addSubItem(bankItem.release());
        }
        parent->addSubItem(fileItem.release());
    };

    // Build tree: each directory entry becomes a root-level category
    // Order: Current JSFX, Custom dirs, Repositories, REAPER
    for (const auto& dirEntry : presetDirectories)
    {
        auto categoryItem = std::make_unique<PresetTreeItem>(
            dirEntry.displayName,
            PresetTreeItem::ItemType::Directory,
            dirEntry.directory,
            juce::String(),
            juce::String(),
            juce::String(),
            this
        );

        // Add all files from this directory
        for (const auto& fileEntry : dirEntry.files)
            addFileTree(categoryItem.get(), fileEntry);

        // Only add category if it has content
        if (categoryItem->getNumSubItems() > 0)
            root->addSubItem(categoryItem.release());
    }

    // Open top-level categories by default (unless auto-hide is enabled)
    if (!isAutoHideEnabled())
        for (int i = 0; i < root->getNumSubItems(); ++i)
            if (auto* item = root->getSubItem(i))
                item->setOpen(true);

    return root;
}

void PresetTreeView::onSelectionChanged()
{
    if (onSelectionChangedCallback)
        onSelectionChangedCallback();
}

void PresetTreeView::onEnterKeyPressed(juce::TreeViewItem* selectedItem)
{
    // Apply preset when Enter is pressed on a preset item
    if (auto* presetItem = dynamic_cast<PresetTreeItem*>(selectedItem))
    {
        if (presetItem->getType() == PresetTreeItem::ItemType::Preset)
            applyPreset(presetItem->getPresetData());
    }
}

void PresetTreeView::onBrowseMenuItemSelected(juce::TreeViewItem* selectedItem)
{
    // Execute command (same as Enter key / double-click) - this will trigger onCommand callback
    if (auto* presetItem = dynamic_cast<PresetTreeItem*>(selectedItem))
    {
        if (presetItem->getType() == PresetTreeItem::ItemType::Preset)
        {
            juce::Array<juce::TreeViewItem*> items;
            items.add(selectedItem);
            executeCommand(items);
        }
    }
}

bool PresetTreeView::shouldIncludeInSearch(juce::TreeViewItem* item)
{
    // Include all items in search
    return true;
}

juce::Array<PresetTreeItem*> PresetTreeView::getSelectedPresetItems()
{
    juce::Array<PresetTreeItem*> items;
    if (auto* root = getRootItem())
        collectSelectedPresetItems(items, root);
    return items;
}

void PresetTreeView::collectSelectedPresetItems(juce::Array<PresetTreeItem*>& items, juce::TreeViewItem* item)
{
    if (auto* presetItem = dynamic_cast<PresetTreeItem*>(item))
    {
        if (presetItem->isSelected())
            items.add(presetItem);
    }

    for (int i = 0; i < item->getNumSubItems(); ++i)
        collectSelectedPresetItems(items, item->getSubItem(i));
}

void PresetTreeView::applyPreset(const juce::String& base64Data)
{
    if (base64Data.isEmpty())
        return;

    processor.loadPresetFromBase64(base64Data);
}

// ============================================================================
// Browse Menu Support
// ============================================================================

juce::Array<juce::TreeViewItem*> PresetTreeView::getDeepestLevelItems()
{
    juce::Array<juce::TreeViewItem*> presets;

    std::function<void(juce::TreeViewItem*)> collectPresets = [&](juce::TreeViewItem* item)
    {
        if (!item)
            return;

        if (auto* presetItem = dynamic_cast<PresetTreeItem*>(item))
        {
            if (presetItem->getType() == PresetTreeItem::ItemType::Preset)
            {
                presets.add(item);
                return; // Don't recurse into preset items
            }
        }

        // Recurse into children
        for (int i = 0; i < item->getNumSubItems(); ++i)
            collectPresets(item->getSubItem(i));
    };

    if (auto* root = getRootItem())
        collectPresets(root);

    return presets;
}

juce::String PresetTreeView::getParentCategoryForItem(juce::TreeViewItem* item)
{
    if (!item)
        return "Uncategorized";

    auto* presetItem = dynamic_cast<PresetTreeItem*>(item);
    if (!presetItem || presetItem->getType() != PresetTreeItem::ItemType::Preset)
        return "Uncategorized";

    // Get the parent (should be Bank or File)
    auto* parent = item->getParentItem();
    if (!parent)
        return "Uncategorized";

    auto* parentPresetItem = dynamic_cast<PresetTreeItem*>(parent);
    if (!parentPresetItem)
        return "Uncategorized";

    // If parent is a Bank, use bank name
    if (parentPresetItem->getType() == PresetTreeItem::ItemType::Bank)
        return parentPresetItem->getName();

    // If parent is a File, use file name (without extension)
    if (parentPresetItem->getType() == PresetTreeItem::ItemType::File)
        return parentPresetItem->getFile().getFileNameWithoutExtension();

    // Fallback to parent name
    return parentPresetItem->getName();
}
