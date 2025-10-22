#include "JsfxPluginTreeView.h"
#include "PluginProcessor.h"
#include "Config.h"

//==============================================================================
// JsfxPluginTreeItem Implementation
//==============================================================================

JsfxPluginTreeItem::JsfxPluginTreeItem(
    const juce::String& name,
    ItemType t,
    const juce::File& file,
    JsfxPluginTreeView* view,
    const ReaPackIndexParser::JsfxEntry& entry
)
    : itemName(name)
    , type(t)
    , pluginFile(file)
    , reapackEntry(entry)
    , pluginTreeView(view)
{
}

bool JsfxPluginTreeItem::mightContainSubItems()
{
    // Categories and RemoteRepo items can contain sub-items
    return type == ItemType::Category || type == ItemType::RemoteRepo;
}

bool JsfxPluginTreeItem::canBeSelected() const
{
    // Metadata items cannot be selected - skip them during navigation
    return type != ItemType::Metadata;
}

void JsfxPluginTreeItem::itemDoubleClicked(const juce::MouseEvent&)
{
    // Load plugin directly on double-click (don't use executeCommand to avoid duplicate calls)
    if (type == ItemType::Plugin && pluginTreeView && pluginFile.existsAsFile())
        pluginTreeView->loadPlugin(pluginFile);
    else if (type == ItemType::RemotePlugin && pluginTreeView)
        pluginTreeView->loadRemotePlugin(reapackEntry);
}

void JsfxPluginTreeItem::itemSelectionChanged(bool isNowSelected)
{
    // Metadata items are now non-selectable via canBeSelected(), so no reactive deselection needed
    repaintItem();

    // Notify the tree view of selection change
    if (pluginTreeView)
        pluginTreeView->onSelectionChanged();
}

void JsfxPluginTreeItem::itemClicked(const juce::MouseEvent& e)
{
    // Show context menu on right-click for all remote item types
    if (e.mods.isPopupMenu()
        && pluginTreeView
        && (type == ItemType::RemotePlugin || type == ItemType::RemoteRepo || type == ItemType::Category))
    {
        // Helper to recursively collect all RemotePlugin items under a tree item
        std::function<void(juce::TreeViewItem*, juce::Array<JsfxPluginTreeItem*>&)> collectRemotePlugins;
        collectRemotePlugins = [&](juce::TreeViewItem* item, juce::Array<JsfxPluginTreeItem*>& items)
        {
            if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
            {
                if (pluginItem->getType() == ItemType::RemotePlugin)
                    items.add(pluginItem);
            }

            for (int i = 0; i < item->getNumSubItems(); ++i)
                collectRemotePlugins(item->getSubItem(i), items);
        };

        // Get all selected items
        auto selectedItems = pluginTreeView->getSelectedPluginItems();

        // Collect RemotePlugin items recursively from selected items
        juce::Array<JsfxPluginTreeItem*> remoteItems;

        if (!selectedItems.isEmpty())
        {
            // If we have selections, collect from all selected items recursively
            for (auto* item : selectedItems)
                collectRemotePlugins(item, remoteItems);
        }

        // If this item isn't in the selection or no remote items found, use this item's tree
        if (!isSelected() || remoteItems.isEmpty())
        {
            remoteItems.clear();
            collectRemotePlugins(this, remoteItems);
        }

        // If still no items (shouldn't happen for RemotePlugin), return
        if (remoteItems.isEmpty())
            return;

        int numItems = remoteItems.size();
        juce::String itemsText = numItems > 1 ? juce::String(numItems) + " packages" : "package";

        // Check pin/cache status for all items
        bool allPinned = true;
        bool anyPinned = false;
        bool anyCached = false;

        for (auto* item : remoteItems)
        {
            bool isPinned = pluginTreeView->isPackagePinned(item->getReaPackEntry().name);
            bool isCached = pluginTreeView->isPackageCached(item->getReaPackEntry());

            if (isPinned)
                anyPinned = true;
            else
                allPinned = false;

            if (isCached)
                anyCached = true;
        }

        juce::PopupMenu menu;

        // Pin/Unpin option
        if (allPinned)
            menu.addItem(1, "Unpin " + itemsText);
        else if (anyPinned)
            menu.addItem(1, "Pin/Unpin " + itemsText);
        else
            menu.addItem(1, "Pin " + itemsText);

        // Download option
        menu.addItem(2, "Download " + itemsText);

        // Clear cache option (only if any are cached)
        if (anyCached)
            menu.addItem(3, "Clear cache for " + itemsText);

        menu.showMenuAsync(
            juce::PopupMenu::Options(),
            [this, remoteItems, allPinned](int result)
            {
                if (!pluginTreeView)
                    return;

                if (result == 1) // Pin/Unpin
                {
                    for (auto* item : remoteItems)
                    {
                        juce::String packageName = item->getReaPackEntry().name;
                        pluginTreeView->setPinned(packageName, !allPinned);
                        item->repaintItem();
                    }
                }
                else if (result == 2) // Download
                {
                    // Download multiple items without loading them
                    bool shouldLoad = (remoteItems.size() == 1);
                    for (auto* item : remoteItems)
                        pluginTreeView->loadRemotePlugin(item->getReaPackEntry(), shouldLoad);
                }
                else if (result == 3) // Clear cache
                {
                    for (auto* item : remoteItems)
                        pluginTreeView->clearPackageCache(item->getReaPackEntry());
                }
            }
        );
    }
}

void JsfxPluginTreeItem::paintItem(juce::Graphics& g, int width, int height)
{
    // Draw simple dark background for downloading items (glow effects are drawn in overlay)
    if (isDownloading)
    {
        // Draw dark background
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillAll();
    }
    else
    {
        // Draw normal match highlight (handles selection, focus, and match states)
        paintMatchHighlight(g, width, height);
    }

    // Metadata items are styled differently (grey and smaller font)
    g.setColour(type == ItemType::Metadata ? juce::Colours::grey : juce::Colours::white);
    g.setFont(juce::Font(type == ItemType::Metadata ? 11.0f : 14.0f));

    const int leftMargin = 4;
    const int rightMargin = 4;

    // For RemotePlugin items, add status indicators
    if (type == ItemType::RemotePlugin && pluginTreeView)
    {
        juce::String statusText;
        int statusWidth = 0;

        // Build status indicators from right to left
        bool isCached = pluginTreeView->isPackageCached(reapackEntry);
        bool isPinned = pluginTreeView->isPackagePinned(reapackEntry.name);
        bool hasUpdate = pluginTreeView->isUpdateAvailable(reapackEntry);

        // Downloading indicator (highest priority - leftmost)
        if (isDownloading)
        {
            statusText = juce::String(juce::CharPointer_UTF8("\xE2\xAC\x87")) + statusText; // ⬇ down arrow
            statusWidth += 20;
        }

        // Cached indicator (small x)
        if (isCached)
        {
            if (statusText.isNotEmpty())
                statusText = "  " + statusText;
            statusText = juce::String(juce::CharPointer_UTF8("\xC3\x97")) + statusText; // × symbol
            statusWidth += 15;
        }

        // Pinned indicator
        if (isPinned)
        {
            if (statusText.isNotEmpty())
                statusText = "  " + statusText;
            statusText = juce::String(juce::CharPointer_UTF8("\xF0\x9F\x93\x8C")) + statusText; // 📌 emoji
            statusWidth += 20;
        }

        // Update available indicator (only if not pinned)
        if (hasUpdate && !isPinned)
        {
            if (statusText.isNotEmpty())
                statusText = "  " + statusText;
            statusText = juce::String(juce::CharPointer_UTF8("\xE2\xAC\x86")) + statusText; // ⬆ arrow
            statusWidth += 20;
        }

        // Draw package name on the left
        g.drawText(
            itemName,
            leftMargin,
            0,
            width - leftMargin - statusWidth - rightMargin - 10,
            height,
            juce::Justification::centredLeft,
            true
        );

        // Draw status indicators on the right
        if (statusText.isNotEmpty())
        {
            // Synthwave colors for downloading indicator with glow effect
            if (isDownloading)
            {
                // Create pulsing glow effect for download indicator
                auto currentTime = juce::Time::getMillisecondCounterHiRes();
                auto glowPhase = std::fmod(currentTime / 600.0, 1.0);
                auto glowAlpha =
                    static_cast<float>(0.6f + 0.4f * std::sin(glowPhase * juce::MathConstants<double>::twoPi));

                auto cyan = juce::Colour(0x00, 0xff, 0xff);
                auto magenta = juce::Colour(0xff, 0x00, 0xff);

                // Alternate between cyan and magenta for extra retro effect
                auto primaryColor = (static_cast<int>(currentTime / 400.0) % 2 == 0) ? cyan : magenta;

                // Draw glow halo first (larger, more transparent)
                g.setColour(primaryColor.withAlpha(glowAlpha * 0.3f));
                g.drawText(
                    statusText,
                    width - statusWidth - rightMargin - 2,
                    -1,
                    statusWidth + 4,
                    height + 2,
                    juce::Justification::centredRight,
                    false
                );

                // Draw main text with bright color
                g.setColour(primaryColor.withAlpha(glowAlpha));
            }
            else
            {
                g.setColour(juce::Colours::grey);
            }

            g.drawText(
                statusText,
                width - statusWidth - rightMargin,
                0,
                statusWidth,
                height,
                juce::Justification::centredRight,
                false
            );
        }
    }
    else
    {
        // Simple text rendering for all other types
        g.drawText(itemName, leftMargin, 0, width - leftMargin - 4, height, juce::Justification::centredLeft, true);
    }
}

int JsfxPluginTreeItem::getItemHeight() const
{
    // Check if item is hidden (filtered out) - return 0 if so
    if (isHidden)
        return 0;

    // Metadata items are shorter
    if (type == ItemType::Metadata)
        return 18;

    // Default height for all other types
    return 20;
} //==============================================================================

// JsfxPluginTreeView Implementation
//==============================================================================

JsfxPluginTreeView::JsfxPluginTreeView(AudioPluginAudioProcessor& proc)
    : processor(proc)
    , downloader(std::make_unique<ReaPackDownloader>())
{
    // Load saved repositories and pinned packages (from reapack.xml)
    loadSavedRepositories();
}

JsfxPluginTreeView::~JsfxPluginTreeView()
{
    // Mark as destroyed to prevent callbacks from accessing this object
    isDestroyed = true;

    // Stop the timer first
    stopTimer();

    // Clear callbacks to prevent any pending async operations from accessing destroyed object
    onSelectionChangedCallback = nullptr;
    onPluginLoadedCallback = nullptr;

    // Destroy downloader before other members to cancel pending downloads
    downloader.reset();
}

void JsfxPluginTreeView::loadPlugins(const juce::StringArray& directoryPaths)
{
    categories.clear();

    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);

    // 1. Add custom directories from user preferences
    for (const auto& path : directoryPaths)
    {
        auto sanitizedPath = path.trim().unquoted();
        juce::File dir(sanitizedPath);

        if (!dir.exists() || !dir.isDirectory())
            continue;

        // Check if this directory is already in our categories
        bool isDuplicate = false;
        for (const auto& cat : categories)
        {
            if (cat.directory == dir)
            {
                isDuplicate = true;
                break;
            }
        }

        if (!isDuplicate)
        {
            CategoryEntry customCategory;
            customCategory.displayName = dir.getFileName();
            customCategory.directory = dir;
            customCategory.isStandardCategory = false; // Mark as custom
            categories.add(customCategory);
        }
    }

    // 3. Repositories are added separately via loadRemoteRepositories()
    // They appear after custom directories in refreshTree()

    // 4. Add REAPER category last
    CategoryEntry reaperCategory;
    reaperCategory.displayName = "REAPER";
    reaperCategory.directory = appDataDir.getChildFile("REAPER").getChildFile("Effects");
    reaperCategory.isStandardCategory = true;
    categories.add(reaperCategory);

    // Rebuild tree
    refreshTree();
}

void JsfxPluginTreeView::loadRemoteRepositories()
{
    // Download and parse remote repository indexes (with caching)
    for (auto& repo : remoteRepositories)
    {
        if (repo.isLoaded)
            continue;

        // Load from cache synchronously
        auto entries = downloader->getCachedIndex(juce::URL(repo.indexUrl));

        if (!entries.empty())
        {
            repo.entries = std::move(entries);
            repo.isLoaded = true;
        }
        // If not in cache, entries will remain empty and isLoaded stays false
        // User can manually refresh to download
    }

    // Refresh tree to show loaded entries
    refreshTree();
}

void JsfxPluginTreeView::scanDirectory(JsfxPluginTreeItem* parentItem, const juce::File& directory, bool recursive)
{
    if (!directory.exists() || !directory.isDirectory())
        return;

    auto files = directory.findChildFiles(juce::File::findFiles, recursive, "*.jsfx");

    for (const auto& file : files)
    {
        auto pluginItem = std::make_unique<JsfxPluginTreeItem>(
            file.getFileNameWithoutExtension(),
            JsfxPluginTreeItem::ItemType::Plugin,
            file,
            this
        );

        parentItem->addSubItem(pluginItem.release());
    }
}

juce::Array<JsfxPluginTreeItem*> JsfxPluginTreeView::getSelectedPluginItems()
{
    juce::Array<JsfxPluginTreeItem*> items;
    if (auto* root = getRootItem())
        collectSelectedPluginItems(items, root);
    return items;
}

void JsfxPluginTreeView::collectSelectedPluginItems(juce::Array<JsfxPluginTreeItem*>& items, juce::TreeViewItem* item)
{
    if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
    {
        if (pluginItem->isSelected()
            && (pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin
                || pluginItem->getType() == JsfxPluginTreeItem::ItemType::RemotePlugin))
            items.add(pluginItem);
    }

    for (int i = 0; i < item->getNumSubItems(); ++i)
        collectSelectedPluginItems(items, item->getSubItem(i));
}

void JsfxPluginTreeView::loadPlugin(const juce::File& pluginFile)
{
    if (!pluginFile.existsAsFile())
    {
        if (onPluginLoadedCallback)
            onPluginLoadedCallback(pluginFile.getFullPathName(), false);
        return;
    }

    bool success = processor.loadJSFX(pluginFile);

    if (onPluginLoadedCallback)
        onPluginLoadedCallback(pluginFile.getFullPathName(), success);
}

void JsfxPluginTreeView::loadRemotePlugin(const ReaPackIndexParser::JsfxEntry& entry, bool loadAfterDownload)
{
    // Mark item as downloading
    setItemDownloading(entry.name, true);

    // Always use downloadJsfx - it handles cache internally with proper async callback
    auto expectedFile = downloader->getCachedFile(entry);
    downloader->downloadJsfx(
        entry,
        [this, entry, expectedFile, loadAfterDownload](const ReaPackDownloader::DownloadResult& result)
        {
            if (isDestroyed)
                return;

            // Clear downloading state
            setItemDownloading(entry.name, false);

            if (result.success)
            {
                // Update cached package info in reapack.xml
                updateCachedPackageInfo(entry.name, entry.version, entry.timestamp);

                // Only load if requested (for single downloads)
                if (loadAfterDownload)
                    loadPlugin(result.downloadedFile);
            }
            else
            {
                if (onPluginLoadedCallback)
                    onPluginLoadedCallback(expectedFile.getFullPathName(), false);

                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Download Failed",
                    "Failed to download " + entry.name + ": " + result.errorMessage
                );
            }
        }
    );
}

void JsfxPluginTreeView::addRemoteEntries(
    JsfxPluginTreeItem* repoItem,
    const std::vector<ReaPackIndexParser::JsfxEntry>& entries
)
{
    // Group entries by category
    std::map<juce::String, std::vector<ReaPackIndexParser::JsfxEntry>> categorized;

    for (const auto& entry : entries)
        categorized[entry.category].push_back(entry);

    // Create category items
    for (const auto& [categoryName, categoryEntries] : categorized)
    {
        auto categoryItem = std::make_unique<JsfxPluginTreeItem>(
            categoryName,
            JsfxPluginTreeItem::ItemType::Category,
            juce::File(),
            this
        );

        // Add plugins to category
        for (const auto& entry : categoryEntries)
        {
            auto pluginItem = std::make_unique<JsfxPluginTreeItem>(
                entry.name,
                JsfxPluginTreeItem::ItemType::RemotePlugin,
                juce::File(),
                this,
                entry
            );

            categoryItem->addSubItem(pluginItem.release());

            // Add metadata items as siblings (children of category, not package)
            if (entry.author.isNotEmpty())
            {
                auto metadataItem = std::make_unique<JsfxPluginTreeItem>(
                    "  Author: " + entry.author,
                    JsfxPluginTreeItem::ItemType::Metadata,
                    juce::File(),
                    this
                );
                categoryItem->addSubItem(metadataItem.release());
            }

            if (entry.version.isNotEmpty())
            {
                // Show only date part of timestamp (YYYY-MM-DD from "2024-10-28T19:21:56Z")
                juce::String versionDisplay = entry.version.substring(0, 10);
                auto metadataItem = std::make_unique<JsfxPluginTreeItem>(
                    "  Version: " + versionDisplay,
                    JsfxPluginTreeItem::ItemType::Metadata,
                    juce::File(),
                    this
                );
                categoryItem->addSubItem(metadataItem.release());
            }

            if (entry.description.isNotEmpty())
            {
                auto metadataItem = std::make_unique<JsfxPluginTreeItem>(
                    "  Description: " + entry.description,
                    JsfxPluginTreeItem::ItemType::Metadata,
                    juce::File(),
                    this
                );
                categoryItem->addSubItem(metadataItem.release());
            }
        }

        if (categoryItem->getNumSubItems() > 0)
            repoItem->addSubItem(categoryItem.release());
    }
}

std::unique_ptr<juce::TreeViewItem> JsfxPluginTreeView::createRootItem()
{
    auto root = std::make_unique<JsfxPluginTreeItem>("Root", JsfxPluginTreeItem::ItemType::Category);

    // Create category items for standard categories only
    for (const auto& category : categories)
    {
        if (!category.isStandardCategory)
            continue; // Skip custom categories - they'll be added as root items below

        auto categoryItem = std::make_unique<JsfxPluginTreeItem>(
            category.displayName,
            JsfxPluginTreeItem::ItemType::Category,
            juce::File(),
            this
        );

        // Scan category directory recursively
        scanDirectory(categoryItem.get(), category.directory, true);

        // Only add category if it has plugins
        if (categoryItem->getNumSubItems() > 0)
            root->addSubItem(categoryItem.release());
    }

    // Add custom directories as root-level items (like repositories)
    for (const auto& category : categories)
    {
        if (category.isStandardCategory)
            continue; // Already added above

        auto customDirItem = std::make_unique<JsfxPluginTreeItem>(
            category.displayName,
            JsfxPluginTreeItem::ItemType::Category,
            juce::File(),
            this
        );

        // Scan custom directory recursively
        scanDirectory(customDirItem.get(), category.directory, true);

        // Only add if it has plugins
        if (customDirItem->getNumSubItems() > 0)
            root->addSubItem(customDirItem.release());
    }

    // Add remote repositories
    for (auto& repo : remoteRepositories)
    {
        auto repoItem = std::make_unique<JsfxPluginTreeItem>(
            repo.name,
            JsfxPluginTreeItem::ItemType::RemoteRepo,
            juce::File(),
            this
        );

        // If repository is loaded, add its entries
        if (repo.isLoaded && !repo.entries.empty())
            addRemoteEntries(repoItem.get(), repo.entries);

        // Always add repo item (it will show "loading..." or entries)
        root->addSubItem(repoItem.release());
    }

    return root;
}

void JsfxPluginTreeView::onSelectionChanged()
{
    if (onSelectionChangedCallback)
        onSelectionChangedCallback();
}

void JsfxPluginTreeView::onEnterKeyPressed(juce::TreeViewItem* selectedItem)
{
    // NOTE: This is intentionally empty/disabled.
    // The actual loading is handled by the onCommand callback set in JsfxPluginWindow.
    // If we implement this here, it causes duplicate loads (both callback and virtual method).
}

void JsfxPluginTreeView::onBrowseMenuItemSelected(juce::TreeViewItem* selectedItem)
{
    if (!selectedItem)
        return;

    if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(selectedItem))
    {
        if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin)
            loadPlugin(pluginItem->getFile());
        else if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::RemotePlugin)
            loadRemotePlugin(pluginItem->getReaPackEntry());
    }
}

bool JsfxPluginTreeView::shouldIncludeInSearch(juce::TreeViewItem* item)
{
    // Search plugin items and remote plugin items, not categories
    if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
    {
        return pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin
            || pluginItem->getType() == JsfxPluginTreeItem::ItemType::RemotePlugin;
    }
    return false;
}

juce::Array<juce::TreeViewItem*> JsfxPluginTreeView::getDeepestLevelItems()
{
    juce::Array<juce::TreeViewItem*> items;

    if (!getRootItem())
        return items;

    // Collect all plugin items from all categories
    std::function<void(juce::TreeViewItem*)> collectPlugins = [&](juce::TreeViewItem* item)
    {
        if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
        {
            if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin
                || pluginItem->getType() == JsfxPluginTreeItem::ItemType::RemotePlugin)
            {
                items.add(item);
                return;
            }
        }

        for (int i = 0; i < item->getNumSubItems(); ++i)
            collectPlugins(item->getSubItem(i));
    };

    collectPlugins(getRootItem());
    return items;
}

juce::String JsfxPluginTreeView::getParentCategoryForItem(juce::TreeViewItem* item)
{
    if (!item)
        return {};

    // Walk up the tree to find the category
    auto* parent = item->getParentItem();
    while (parent != nullptr && parent != getRootItem())
    {
        if (auto* categoryItem = dynamic_cast<JsfxPluginTreeItem*>(parent))
        {
            if (categoryItem->getType() == JsfxPluginTreeItem::ItemType::Category)
                return categoryItem->getName();
        }
        parent = parent->getParentItem();
    }

    return {};
}

std::vector<std::pair<juce::String, juce::String>> JsfxPluginTreeView::getRemoteRepositories() const
{
    std::vector<std::pair<juce::String, juce::String>> result;
    for (const auto& repo : remoteRepositories)
        result.push_back({repo.name, repo.indexUrl});
    return result;
}

void JsfxPluginTreeView::setRemoteRepositories(const std::vector<std::pair<juce::String, juce::String>>& repos)
{
    remoteRepositories.clear();

    for (const auto& [name, url] : repos)
    {
        RemoteRepository repo;
        repo.name = name;
        repo.indexUrl = url;
        repo.isLoaded = false;
        remoteRepositories.add(repo);
    }

    // Save to persistent storage
    saveRepositories();

    // Trigger reload of remote repositories
    loadRemoteRepositories();
}

void JsfxPluginTreeView::loadSavedRepositories()
{
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    auto configFile = appDataDir.getChildFile(PluginConstants::ApplicationName).getChildFile("reapack.xml");

    bool configExists = configFile.existsAsFile();

    if (configExists)
    {
        auto xml = juce::parseXML(configFile);
        if (xml && xml->hasTagName("ReaPack"))
        {
            // Load repositories
            if (auto* reposElement = xml->getChildByName("Repositories"))
            {
                for (auto* repoElement : reposElement->getChildIterator())
                {
                    if (repoElement->hasTagName("Repository"))
                    {
                        RemoteRepository repo;
                        repo.name = repoElement->getStringAttribute("name");
                        repo.indexUrl = repoElement->getStringAttribute("url");
                        repo.isLoaded = false;

                        if (repo.name.isNotEmpty() && repo.indexUrl.isNotEmpty())
                            remoteRepositories.add(repo);
                    }
                }
            }

            // Load pinned packages
            if (auto* pinnedElement = xml->getChildByName("PinnedPackages"))
            {
                for (auto* packageElement : pinnedElement->getChildIterator())
                {
                    if (packageElement->hasTagName("Package"))
                    {
                        juce::String packageName = packageElement->getStringAttribute("name");
                        if (packageName.isNotEmpty())
                            pinnedPackages.add(packageName);
                    }
                }
            }

            // Load cached package versions
            if (auto* cachedElement = xml->getChildByName("CachedPackages"))
            {
                for (auto* packageElement : cachedElement->getChildIterator())
                {
                    if (packageElement->hasTagName("Package"))
                    {
                        CachedPackageInfo info;
                        info.packageName = packageElement->getStringAttribute("name");
                        info.version = packageElement->getStringAttribute("version");
                        info.timestamp = packageElement->getStringAttribute("timestamp");

                        if (info.packageName.isNotEmpty() && info.timestamp.isNotEmpty())
                            cachedPackages.add(info);
                    }
                }
            }
        }
        // Config file exists, respect user's choice (even if repository list is empty)
    }
    else
    {
        // No config file exists - this is first run, fetch and add default repositories
        fetchAndAddDefaultRepository(JUCESONIC_DEFAULT_JSFX_REPO_1_URL);
        fetchAndAddDefaultRepository(JUCESONIC_DEFAULT_JSFX_REPO_2_URL);
    }
}

void JsfxPluginTreeView::fetchAndAddDefaultRepository(const juce::String& url)
{
    // Download and parse index in background to get repository name
    juce::Thread::launch(
        [this, url]()
        {
            auto inputStream = juce::URL(url).createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(10000)
            );

            juce::String repoName;

            if (inputStream != nullptr)
            {
                juce::String xmlContent = inputStream->readEntireStreamAsString();
                repoName = ReaPackIndexParser::getRepositoryName(xmlContent);
            }

            // Add repository on message thread
            juce::MessageManager::callAsync(
                [this, url, repoName]()
                {
                    if (isDestroyed)
                        return;

                    if (repoName.isNotEmpty())
                    {
                        RemoteRepository repo;
                        repo.name = repoName;
                        repo.indexUrl = url;
                        repo.isLoaded = false;
                        remoteRepositories.add(repo);

                        // Save to config file so it persists
                        saveRepositories();

                        // Trigger reload of this repository
                        loadRemoteRepositories();
                    }
                }
            );
        }
    );
}

void JsfxPluginTreeView::saveRepositories()
{
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    auto dataDir = appDataDir.getChildFile(PluginConstants::ApplicationName);
    dataDir.createDirectory();

    auto configFile = dataDir.getChildFile("reapack.xml");

    // Create root element
    juce::XmlElement root("ReaPack");

    // Add repositories section
    auto* reposElement = root.createNewChildElement("Repositories");
    for (const auto& repo : remoteRepositories)
    {
        auto* repoElement = reposElement->createNewChildElement("Repository");
        repoElement->setAttribute("name", repo.name);
        repoElement->setAttribute("url", repo.indexUrl);
    }

    // Add pinned packages section
    auto* pinnedElement = root.createNewChildElement("PinnedPackages");
    for (const auto& packageName : pinnedPackages)
    {
        auto* packageElement = pinnedElement->createNewChildElement("Package");
        packageElement->setAttribute("name", packageName);
    }

    // Add cached packages section
    auto* cachedElement = root.createNewChildElement("CachedPackages");
    for (const auto& pkg : cachedPackages)
    {
        auto* packageElement = cachedElement->createNewChildElement("Package");
        packageElement->setAttribute("name", pkg.packageName);
        packageElement->setAttribute("version", pkg.version);
        packageElement->setAttribute("timestamp", pkg.timestamp);
    }

    root.writeTo(configFile);
}

void JsfxPluginTreeView::updateAllRemotePlugins()
{
    struct UpdateTracker
    {
        std::atomic<int> pendingRepos{0};
        std::atomic<int> pendingDownloads{0};
        std::atomic<int> updatedCount{0};
        std::atomic<int> failedCount{0};
        std::atomic<bool> completionShown{false};
    };

    auto tracker = std::make_shared<UpdateTracker>();
    tracker->pendingRepos.store(static_cast<int>(remoteRepositories.size()));

    if (tracker->pendingRepos.load() == 0)
    {
        juce::MessageManager::callAsync(
            []()
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon,
                    "Update Complete",
                    "No repositories configured."
                );
            }
        );
        return;
    }

    auto finishIfDone = [tracker]()
    {
        if (tracker->pendingRepos.load() == 0 && tracker->pendingDownloads.load() == 0)
        {
            if (!tracker->completionShown.exchange(true))
            {
                const int updated = tracker->updatedCount.load();
                const int failed = tracker->failedCount.load();

                juce::MessageManager::callAsync(
                    [updated, failed]()
                    {
                        juce::String message = "Updated " + juce::String(updated) + " package(s)";
                        if (failed > 0)
                            message += "\n" + juce::String(failed) + " package(s) failed.";

                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Update Complete", message);
                    }
                );
            }
        }
    };

    for (auto& repo : remoteRepositories)
    {
        const juce::String repoUrl = repo.indexUrl;

        downloader->downloadIndex(
            juce::URL(repoUrl),
            [this, tracker, finishIfDone, repoUrl](bool success, std::vector<ReaPackIndexParser::JsfxEntry> entries)
            {
                if (isDestroyed)
                {
                    tracker->pendingRepos.fetch_sub(1);
                    finishIfDone();
                    return;
                }

                if (success)
                {
                    if (auto* targetRepo = findRepositoryByUrl(repoUrl))
                    {
                        targetRepo->entries = entries;
                        targetRepo->isLoaded = true;
                    }

                    for (const auto& entry : entries)
                    {
                        if (isPackagePinned(entry.name))
                            continue;

                        if (!downloader->isCached(entry))
                            continue;

                        juce::String cachedTimestamp = getCachedPackageTimestamp(entry.name);

                        if (cachedTimestamp.isEmpty())
                            continue;

                        if (entry.timestamp > cachedTimestamp)
                        {
                            tracker->pendingDownloads.fetch_add(1);

                            downloader->downloadJsfx(
                                entry,
                                [this, tracker, finishIfDone, entry](const ReaPackDownloader::DownloadResult& result)
                                {
                                    if (isDestroyed)
                                    {
                                        tracker->pendingDownloads.fetch_sub(1);
                                        finishIfDone();
                                        return;
                                    }

                                    if (result.success)
                                    {
                                        updateCachedPackageInfo(entry.name, entry.version, entry.timestamp);
                                        tracker->updatedCount.fetch_add(1);
                                    }
                                    else
                                    {
                                        tracker->failedCount.fetch_add(1);
                                    }

                                    tracker->pendingDownloads.fetch_sub(1);
                                    finishIfDone();
                                }
                            );
                        }
                    }
                }
                else
                {
                    tracker->failedCount.fetch_add(1);
                }

                tracker->pendingRepos.fetch_sub(1);
                finishIfDone();
            },
            true // Force refresh
        );
    }
}

bool JsfxPluginTreeView::isPackagePinned(const juce::String& packageName) const
{
    return pinnedPackages.contains(packageName);
}

bool JsfxPluginTreeView::isPackageCached(const ReaPackIndexParser::JsfxEntry& entry) const
{
    return downloader->isCached(entry);
}

void JsfxPluginTreeView::clearPackageCache(const ReaPackIndexParser::JsfxEntry& entry)
{
    if (!downloader)
        return;

    // Use the downloader to clear the package cache (deletes all files)
    bool wasDeleted = downloader->clearPackageCache(entry);

    if (wasDeleted)
    {
        // Remove from cached package info
        for (int i = cachedPackages.size() - 1; i >= 0; --i)
        {
            if (cachedPackages[i].packageName == entry.name)
            {
                cachedPackages.remove(i);
                break;
            }
        }
        saveRepositories();

        // Trigger repaint to update visual indicators
        if (auto* root = getRootItem())
        {
            std::function<void(juce::TreeViewItem*)> repaintPackage = [&](juce::TreeViewItem* item)
            {
                if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
                {
                    if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::RemotePlugin
                        && pluginItem->getReaPackEntry().name == entry.name)
                    {
                        pluginItem->repaintItem();
                    }
                }

                for (int i = 0; i < item->getNumSubItems(); ++i)
                    repaintPackage(item->getSubItem(i));
            };

            repaintPackage(root);
        }
    }
}

void JsfxPluginTreeView::setPinned(const juce::String& packageName, bool pinned)
{
    if (pinned)
    {
        if (!pinnedPackages.contains(packageName))
            pinnedPackages.add(packageName);
    }
    else
    {
        pinnedPackages.removeString(packageName);
    }

    savePinnedPackages();
}

bool JsfxPluginTreeView::isUpdateAvailable(const ReaPackIndexParser::JsfxEntry& entry) const
{
    // Check if package is cached and has an older version
    if (!downloader->isCached(entry))
        return false;

    juce::String cachedTimestamp = getCachedPackageTimestamp(entry.name);
    if (cachedTimestamp.isEmpty())
        return false;

    // Compare timestamps - if remote is newer, update is available
    return entry.timestamp > cachedTimestamp;
}

void JsfxPluginTreeView::loadPinnedPackages()
{
    // Pinned packages are now loaded in loadSavedRepositories()
    // This method is kept for backwards compatibility but does nothing
}

void JsfxPluginTreeView::savePinnedPackages()
{
    // Pinned packages are now saved in saveRepositories()
    // This method calls saveRepositories() to keep everything in sync
    saveRepositories();
}

void JsfxPluginTreeView::updateCachedPackageInfo(
    const juce::String& packageName,
    const juce::String& version,
    const juce::String& timestamp
)
{
    // Find existing entry or create new one
    for (auto& pkg : cachedPackages)
    {
        if (pkg.packageName == packageName)
        {
            pkg.version = version;
            pkg.timestamp = timestamp;
            saveRepositories();
            return;
        }
    }

    // Not found, add new entry
    CachedPackageInfo info;
    info.packageName = packageName;
    info.version = version;
    info.timestamp = timestamp;
    cachedPackages.add(info);
    saveRepositories();
}

juce::String JsfxPluginTreeView::getCachedPackageTimestamp(const juce::String& packageName) const
{
    for (const auto& pkg : cachedPackages)
        if (pkg.packageName == packageName)
            return pkg.timestamp;
    return {};
}

JsfxPluginTreeView::RemoteRepository* JsfxPluginTreeView::findRepositoryByUrl(const juce::String& url)
{
    for (auto& repo : remoteRepositories)
        if (repo.indexUrl == url)
            return &repo;

    return nullptr;
}

void JsfxPluginTreeView::setItemDownloading(const juce::String& packageName, bool downloading)
{
    // Helper to recursively search for the item
    std::function<void(juce::TreeViewItem*)> findAndSetDownloading = [&](juce::TreeViewItem* item)
    {
        if (!item)
            return;

        if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
        {
            if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::RemotePlugin
                && pluginItem->getReaPackEntry().name == packageName)
            {
                pluginItem->setDownloading(downloading);
                pluginItem->repaintItem();
            }
        }

        for (int i = 0; i < item->getNumSubItems(); ++i)
            findAndSetDownloading(item->getSubItem(i));
    };

    if (auto* root = getRootItem())
        findAndSetDownloading(root);

    // Manage download counter and timer
    if (downloading)
    {
        activeDownloads++;
        if (!isTimerRunning())
            startTimer(16); // 60 fps for smooth synthwave animation
    }
    else
    {
        activeDownloads = juce::jmax(0, activeDownloads - 1);
        if (activeDownloads == 0)
            stopTimer();
    }
}

void JsfxPluginTreeView::timerCallback()
{
    // Repaint all downloading items for animation
    std::function<void(juce::TreeViewItem*)> repaintDownloading = [&](juce::TreeViewItem* item)
    {
        if (!item)
            return;

        if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
        {
            if (pluginItem->getDownloading())
                pluginItem->repaintItem();
        }

        for (int i = 0; i < item->getNumSubItems(); ++i)
            repaintDownloading(item->getSubItem(i));
    };

    if (auto* root = getRootItem())
        repaintDownloading(root);

    // Also repaint the entire tree view for glow overlay
    repaint();
}

void JsfxPluginTreeView::drawDownloadGlowEffects(juce::Graphics& g)
{
    // Draw glow overlays on top of all items (including the tree view itself)
    if (activeDownloads == 0)
        return;

    auto currentTime = juce::Time::getMillisecondCounterHiRes();
    auto timeOffset = currentTime / 1000.0;

    auto cyan = juce::Colour(0x00, 0xff, 0xff);
    auto magenta = juce::Colour(0xff, 0x00, 0xff);

    // Find all downloading items and draw glow overlays
    std::function<void(juce::TreeViewItem*)> drawGlowOverlays = [&](juce::TreeViewItem* item)
    {
        if (!item)
            return;

        if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
        {
            if (pluginItem->getDownloading() && item->isOpen())
            {
                // Get item bounds in tree view coordinates
                auto itemBounds = item->getItemPosition(true);
                auto height = item->getItemHeight();
                auto width = getTreeView().getWidth();

                if (itemBounds.getY() + height < 0 || itemBounds.getY() > getHeight())
                {
                    // Item not visible, skip
                    for (int i = 0; i < item->getNumSubItems(); ++i)
                        drawGlowOverlays(item->getSubItem(i));
                    return;
                }

                // Calculate waveform with spillover
                auto centerY = itemBounds.getY() + height * 0.5f;
                auto amplitude = height * 0.6f; // Larger amplitude for spillover

                // Draw cyan waveform glow layers (spillover effect)
                for (int layer = 5; layer >= 1; layer--)
                {
                    juce::Path waveformPath;
                    bool firstPoint = true;

                    for (int x = 0; x < width; x += 3)
                    {
                        auto xNorm = static_cast<double>(x) / width;
                        auto xPhase = xNorm * juce::MathConstants<double>::twoPi * 3.0;

                        auto fundamental = std::sin(xPhase - timeOffset * 8.0) * 0.6;
                        auto harmonic2 = std::sin(xPhase * 2.0 - timeOffset * 12.0) * 0.35;
                        auto harmonic3 = std::sin(xPhase * 3.0 + std::sin(timeOffset * 4.0)) * 0.2;
                        auto noise =
                            (std::sin(xPhase * 17.3 + timeOffset * 23.7) * std::sin(xPhase * 11.7 - timeOffset * 19.3))
                            * 0.15;

                        auto pulsePhase = std::fmod(timeOffset * 2.0, 1.0);
                        auto pulseDist = std::abs(xNorm - pulsePhase);
                        auto pulse = pulseDist < 0.05 ? std::exp(-pulseDist * 100.0) * 0.7 : 0.0;

                        auto waveValue = fundamental + harmonic2 + harmonic3 + noise + pulse;
                        auto y = centerY + static_cast<float>(waveValue * amplitude);

                        if (firstPoint)
                        {
                            waveformPath.startNewSubPath(static_cast<float>(x), y);
                            firstPoint = false;
                        }
                        else
                        {
                            waveformPath.lineTo(static_cast<float>(x), y);
                        }
                    }

                    // Draw glow layers from outer to inner
                    float strokeWidth = 4.0f * layer;
                    float alpha = (0.12f / layer) * (6 - layer);

                    g.setColour(cyan.withAlpha(alpha));
                    g.strokePath(waveformPath, juce::PathStrokeType(strokeWidth));
                }

                // Draw magenta accent waveform glow
                for (int layer = 3; layer >= 1; layer--)
                {
                    juce::Path accentPath;
                    bool firstPoint = true;

                    for (int x = 0; x < width; x += 3)
                    {
                        auto xNorm = static_cast<double>(x) / width;
                        auto xPhase = xNorm * juce::MathConstants<double>::twoPi * 3.0;

                        auto accent = std::sin(xPhase - timeOffset * 8.0 + juce::MathConstants<double>::pi) * 0.4;
                        auto y = centerY + static_cast<float>(accent * amplitude * 0.7);

                        if (firstPoint)
                        {
                            accentPath.startNewSubPath(static_cast<float>(x), y);
                            firstPoint = false;
                        }
                        else
                        {
                            accentPath.lineTo(static_cast<float>(x), y);
                        }
                    }

                    float strokeWidth = 3.0f * layer;
                    float alpha = (0.15f / layer) * (4 - layer);

                    g.setColour(magenta.withAlpha(alpha));
                    g.strokePath(accentPath, juce::PathStrokeType(strokeWidth));
                }

                // Draw vertical pulse with massive glow
                auto pulseX = static_cast<float>(std::fmod(timeOffset * 2.0, 1.0) * width);

                for (int i = 6; i >= 1; i--)
                {
                    auto glowWidth = 60.0f * i;
                    auto glowAlpha = 0.06f / i;

                    juce::ColourGradient pulseGlow(
                        magenta.withAlpha(glowAlpha),
                        pulseX,
                        centerY,
                        magenta.withAlpha(0.0f),
                        pulseX + glowWidth,
                        centerY,
                        true
                    );
                    g.setGradientFill(pulseGlow);
                    g.fillRect(
                        juce::Rectangle<float>(
                            pulseX - glowWidth,
                            centerY - height * 3.0f,
                            glowWidth * 2,
                            height * 6.0f
                        )
                    );
                }

                g.setColour(magenta.withAlpha(0.7f));
                g.drawLine(pulseX, centerY - height * 2.5f, pulseX, centerY + height * 2.5f, 3.0f);

                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.drawLine(pulseX, centerY - height * 2.5f, pulseX, centerY + height * 2.5f, 1.5f);
            }
        }

        for (int i = 0; i < item->getNumSubItems(); ++i)
            drawGlowOverlays(item->getSubItem(i));
    };

    if (auto* root = getRootItem())
        drawGlowOverlays(root);
}
