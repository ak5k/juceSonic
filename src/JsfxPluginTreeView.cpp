#include "JsfxPluginTreeView.h"
#include "PluginProcessor.h"

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
}

void JsfxPluginTreeItem::itemClicked(const juce::MouseEvent& e)
{
    // Show context menu on right-click for RemotePlugin items
    if (e.mods.isPopupMenu() && type == ItemType::RemotePlugin && pluginTreeView)
    {
        juce::PopupMenu menu;

        bool isPinned = pluginTreeView->isPackagePinned(reapackEntry.name);
        menu.addItem(1, isPinned ? "Unpin Package" : "Pin Package");

        menu.showMenuAsync(
            juce::PopupMenu::Options(),
            [this, isPinned](int result)
            {
                if (result == 1 && pluginTreeView)
                {
                    pluginTreeView->setPinned(reapackEntry.name, !isPinned);
                    repaintItem(); // Update visual indicator if needed
                }
            }
        );
    }
}

void JsfxPluginTreeItem::paintItem(juce::Graphics& g, int width, int height)
{
    // Draw match highlight (handles selection, focus, and match states)
    paintMatchHighlight(g, width, height);

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

        // Cached indicator (small x)
        if (isCached)
        {
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
            g.setColour(juce::Colours::grey);
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

JsfxPluginTreeView::~JsfxPluginTreeView() = default;

void JsfxPluginTreeView::loadPlugins(const juce::StringArray& directoryPaths)
{
    categories.clear();

    // Define standard categories
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    auto juceSonicDataDir =
        appDataDir.getChildFile(PluginConstants::ApplicationName).getChildFile(PluginConstants::DataDirectoryName);

    // User category: <appdata>/juceSonic/data/user
    CategoryEntry userCategory;
    userCategory.displayName = "User";
    userCategory.directory = juceSonicDataDir.getChildFile(PluginConstants::UserPresetsDirectoryName);

    // Local category: <appdata>/juceSonic/data/local
    CategoryEntry localCategory;
    localCategory.displayName = "Local";
    localCategory.directory = juceSonicDataDir.getChildFile(PluginConstants::LocalPresetsDirectoryName);

    // Remote category: <appdata>/juceSonic/data/remote
    CategoryEntry remoteCategory;
    remoteCategory.displayName = "Remote";
    remoteCategory.directory = juceSonicDataDir.getChildFile(PluginConstants::RemotePresetsDirectoryName);

    // REAPER category: <appdata>/REAPER/Effects
    CategoryEntry reaperCategory;
    reaperCategory.displayName = "REAPER";
    reaperCategory.directory = appDataDir.getChildFile("REAPER").getChildFile("Effects");

    // Add standard categories
    categories.add(userCategory);
    categories.add(localCategory);
    categories.add(remoteCategory);
    categories.add(reaperCategory);

    // Add additional custom directories from user preferences
    for (const auto& path : directoryPaths)
    {
        juce::File dir(path);
        if (!dir.exists() || !dir.isDirectory())
            continue;

        // Check if this directory is already in our standard categories
        bool isStandardCategory = false;
        for (const auto& cat : categories)
        {
            if (cat.directory == dir)
            {
                isStandardCategory = true;
                break;
            }
        }

        if (!isStandardCategory)
        {
            CategoryEntry customCategory;
            customCategory.displayName = dir.getFileName();
            customCategory.directory = dir;
            categories.add(customCategory);
        }
    }

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

        // Try to load from cache first (forceRefresh=false)
        downloader->downloadIndex(
            juce::URL(repo.indexUrl),
            [this, repoUrl = repo.indexUrl](bool success, std::vector<ReaPackIndexParser::JsfxEntry> entries)
            {
                if (success)
                {
                    if (auto* targetRepo = findRepositoryByUrl(repoUrl))
                    {
                        targetRepo->entries = std::move(entries);
                        targetRepo->isLoaded = true;

                        // Refresh tree to show new entries
                        juce::MessageManager::callAsync([this]() { refreshTree(); });
                    }
                }
            },
            false
        ); // Don't force refresh - use cache if available
    }
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

void JsfxPluginTreeView::loadRemotePlugin(const ReaPackIndexParser::JsfxEntry& entry)
{
    // Always use downloadJsfx - it handles cache internally with proper async callback
    auto expectedFile = downloader->getCachedFile(entry);
    downloader->downloadJsfx(
        entry,
        [this, entry, expectedFile](const ReaPackDownloader::DownloadResult& result)
        {
            if (result.success)
            {
                // Update cached package info in reapack.xml
                updateCachedPackageInfo(entry.name, entry.version, entry.timestamp);

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

    // Create category items and scan directories
    for (const auto& category : categories)
    {
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

    if (configFile.existsAsFile())
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
    }

    // If no repositories loaded, add default
    if (remoteRepositories.isEmpty())
    {
        RemoteRepository reaTeamRepo;
        reaTeamRepo.name = "ReaTeam JSFX";
        reaTeamRepo.indexUrl = "https://github.com/ReaTeam/JSFX/raw/master/index.xml";
        remoteRepositories.add(reaTeamRepo);
    }
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
