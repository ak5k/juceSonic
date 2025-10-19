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
    // Apply preset on double-click if this is a preset item
    if (type == ItemType::Preset && presetTreeView && !data.isEmpty())
        presetTreeView->applyPreset(data);
}

void PresetTreeItem::itemSelectionChanged(bool isNowSelected)
{
    repaintItem();
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

    // Determine which directory is the default install root
    auto defaultInstallRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getChildFile("juceSonic")
                                  .getChildFile("data")
                                  .getChildFile("local")
                                  .getFullPathName();

    // Determine the data directory to identify external directories
    auto dataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("juceSonic")
                       .getChildFile("data")
                       .getFullPathName();

    // Determine the remote directory
    auto remoteDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("juceSonic")
                         .getChildFile("data")
                         .getChildFile("remote")
                         .getFullPathName();

    for (const auto& path : directoryPaths)
    {
        juce::File dir(path);
        if (!dir.exists() || !dir.isDirectory())
            continue;

        DirectoryEntry dirEntry;
        dirEntry.directory = dir;

        // Check if this is the default install root - if so, scan recursively
        bool isDefaultRoot = (path == defaultInstallRoot);
        dirEntry.isDefaultRoot = isDefaultRoot;

        // Check if this is from the remote directory - if so, scan recursively
        bool isRemote = path.startsWith(remoteDir);
        dirEntry.isRemoteRoot = isRemote;

        // Check if this is an external directory (outside the data directory)
        bool isExternal = !isDefaultRoot && !isRemote && !path.startsWith(dataDir);
        dirEntry.isExternalRoot = isExternal;

        // Scan recursively for default root and remote directories
        bool scanRecursively = isDefaultRoot || isRemote;

        // Find all .rpl files in this directory
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

    // Determine directory paths for categorization
    auto defaultInstallRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getChildFile("juceSonic")
                                  .getChildFile("data")
                                  .getChildFile("local");

    auto dataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("juceSonic")
                       .getChildFile("data");

    auto remoteDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("juceSonic")
                         .getChildFile("data")
                         .getChildFile("remote");

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
            // Determine which scan root this file belongs to
            juce::File scanRoot;

            if (fileEntry.file.isAChildOf(defaultInstallRoot))
            {
                scanRoot = defaultInstallRoot;
            }
            else if (fileEntry.file.isAChildOf(remoteDir))
            {
                scanRoot = remoteDir;
            }
            else
            {
                // External directory - use the immediate parent of the file as the scan root
                scanRoot = fileEntry.file.getParentDirectory();
            }

            filesByScanRoot[scanRoot].push_back(fileEntry);
        }
    }

    // Create directory entries for each scan root
    for (const auto& pair : filesByScanRoot)
    {
        DirectoryEntry dirEntry;
        dirEntry.directory = pair.first;
        dirEntry.files = pair.second;

        // Determine directory type
        auto dirPath = pair.first.getFullPathName();
        auto defaultRootPath = defaultInstallRoot.getFullPathName();
        auto remoteDirPath = remoteDir.getFullPathName();
        auto dataDirPath = dataDir.getFullPathName();

        dirEntry.isDefaultRoot = (dirPath == defaultRootPath);
        dirEntry.isRemoteRoot = dirPath.startsWith(remoteDirPath);
        dirEntry.isExternalRoot = !dirEntry.isDefaultRoot && !dirEntry.isRemoteRoot && !dirPath.startsWith(dataDirPath);

        presetDirectories.push_back(dirEntry);
    }

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

    for (const auto& dirEntry : presetDirectories)
    {
        // Determine display name based on directory type
        juce::String displayName;
        if (dirEntry.isDefaultRoot)
        {
            // Show relative path from local parent, including "local"
            auto localParent = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                   .getChildFile("juceSonic")
                                   .getChildFile("data")
                                   .getChildFile("local");
            displayName = "local/" + dirEntry.directory.getRelativePathFrom(localParent);
        }
        else if (dirEntry.isRemoteRoot)
        {
            // Show relative path from remote parent, including "remote"
            auto remoteParent = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                    .getChildFile("juceSonic")
                                    .getChildFile("data")
                                    .getChildFile("remote");
            displayName = "remote/" + dirEntry.directory.getRelativePathFrom(remoteParent);
        }
        else if (dirEntry.isExternalRoot)
        {
            // Show full absolute path for external
            displayName = dirEntry.directory.getFullPathName();
        }
        else if (dirEntry.directory == juce::File())
        {
            displayName = "local"; // ValueTree-loaded presets with no physical directory
        }
        else
        {
            displayName = dirEntry.directory.getFullPathName();
        }

        DBG("  isDefault: "
            + juce::String(dirEntry.isDefaultRoot ? "true" : "false")
            + ", isRemote: "
            + juce::String(dirEntry.isRemoteRoot ? "true" : "false")
            + ", isExternal: "
            + juce::String(dirEntry.isExternalRoot ? "true" : "false"));

        auto dirItem = std::make_unique<PresetTreeItem>(
            displayName,
            PresetTreeItem::ItemType::Directory,
            dirEntry.directory,
            juce::String(),
            juce::String(),
            juce::String(),
            this
        );

        for (const auto& fileEntry : dirEntry.files)
        {
            auto fileItem = std::make_unique<PresetTreeItem>(
                fileEntry.file.getFileNameWithoutExtension(),
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

            dirItem->addSubItem(fileItem.release());
        }

        root->addSubItem(dirItem.release());
    }

    // Set root directory items to be open by default AFTER they're added to the tree
    // This ensures the openness state is properly applied
    // BUT: If we're in auto-hide mode, keep them collapsed for a cleaner initial appearance
    if (!isAutoHideEnabled())
    {
        for (int i = 0; i < root->getNumSubItems(); ++i)
            if (auto* item = root->getSubItem(i))
                item->setOpen(true);
    }

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
