#pragma once

#include "SearchableTreeView.h"
#include "ReaPackDownloader.h"
#include "ReaPackIndexParser.h"
#include <Config.h>
#include <juce_audio_processors/juce_audio_processors.h>

// Forward declarations
class AudioPluginAudioProcessor;
class JsfxPluginTreeView;

/**
 * @brief Tree item for JSFX plugin browser
 * Supports hierarchical structure for organizing plugins by category
 */
class JsfxPluginTreeItem : public SearchableTreeItem
{
public:
    enum class ItemType
    {
        Category,     // Root category (User, Local, Remote, REAPER)
        Plugin,       // Individual JSFX plugin file
        RemoteRepo,   // Remote repository (e.g., ReaTeam JSFX)
        RemotePlugin, // Plugin from remote repository (not yet downloaded)
        Metadata      // Metadata line (author, version, description)
    };

    JsfxPluginTreeItem(
        const juce::String& name,
        ItemType t,
        const juce::File& file = juce::File(),
        JsfxPluginTreeView* view = nullptr,
        const ReaPackIndexParser::JsfxEntry& entry = {}
    );

    // SearchableTreeItem overrides
    juce::String getName() const override
    {
        return itemName;
    }

    bool mightContainSubItems() override;
    bool canBeSelected() const override;
    void itemDoubleClicked(const juce::MouseEvent& e) override;
    void itemSelectionChanged(bool isNowSelected) override;
    void itemClicked(const juce::MouseEvent& e) override;
    void paintItem(juce::Graphics& g, int width, int height) override;
    int getItemHeight() const override;

    // Accessors
    ItemType getType() const
    {
        return type;
    }

    const juce::File& getFile() const
    {
        return pluginFile;
    }

    const ReaPackIndexParser::JsfxEntry& getReaPackEntry() const
    {
        return reapackEntry;
    }

    void setDownloading(bool downloading)
    {
        isDownloading = downloading;
    }

    bool getDownloading() const
    {
        return isDownloading;
    }

private:
    juce::String itemName;
    ItemType type;
    juce::File pluginFile;                      // For Plugin items
    ReaPackIndexParser::JsfxEntry reapackEntry; // For RemotePlugin items
    JsfxPluginTreeView* pluginTreeView = nullptr;
    bool isDownloading = false;
};

/**
 * @brief Searchable tree view for JSFX plugins
 *
 * Displays JSFX plugin files organized by category:
 * - User: User-installed plugins
 * - Local: Locally available plugins
 * - Remote: Remote/downloaded plugins
 * - REAPER: Plugins from REAPER installation
 *
 * No metadata display needed.
 */
class JsfxPluginTreeView
    : public SearchableTreeView
    , private juce::Timer
{
public:
    explicit JsfxPluginTreeView(AudioPluginAudioProcessor& proc);
    ~JsfxPluginTreeView() override;

    // Callbacks
    std::function<void()> onSelectionChangedCallback;
    std::function<void(const juce::String& pluginPath, bool success)> onPluginLoadedCallback;

    // Load plugins from directory paths
    void loadPlugins(const juce::StringArray& directoryPaths);

    // Load remote repositories
    void loadRemoteRepositories();

    // Repository management
    std::vector<std::pair<juce::String, juce::String>> getRemoteRepositories() const;
    void setRemoteRepositories(const std::vector<std::pair<juce::String, juce::String>>& repos);

    // Update all cached remote plugins (check for newer versions)
    void updateAllRemotePlugins();

    // Pin/unpin remote packages (prevent updates)
    bool isPackagePinned(const juce::String& packageName) const;
    void setPinned(const juce::String& packageName, bool pinned);

    // Check if package is cached
    bool isPackageCached(const ReaPackIndexParser::JsfxEntry& entry) const;

    // Check if update is available for a remote plugin
    bool isUpdateAvailable(const ReaPackIndexParser::JsfxEntry& entry) const;

    // Clear cached files for a package
    void clearPackageCache(const ReaPackIndexParser::JsfxEntry& entry);

    // Load plugin (local file)
    void loadPlugin(const juce::File& pluginFile);

    // Load remote plugin (download if needed, optionally load as JSFX)
    void loadRemotePlugin(const ReaPackIndexParser::JsfxEntry& entry, bool loadAfterDownload = true);

    // Get selected items for operations
    juce::Array<JsfxPluginTreeItem*> getSelectedPluginItems();

    // SearchableTreeView overrides
    std::unique_ptr<juce::TreeViewItem> createRootItem() override;
    void onSelectionChanged() override;
    void onEnterKeyPressed(juce::TreeViewItem* selectedItem) override;
    void onBrowseMenuItemSelected(juce::TreeViewItem* selectedItem) override;

    juce::String getSearchPlaceholder() const override
    {
        return "Type to search plugins...";
    }

    // No metadata needed
    juce::Array<std::pair<juce::String, juce::String>> getMetadataForItem(juce::TreeViewItem* item) override
    {
        return {};
    }

    bool shouldIncludeInSearch(juce::TreeViewItem* item) override;

    bool shouldCountItem(juce::TreeViewItem* item) override
    {
        // Only count actual plugin items, not categories
        if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
            return pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin;
        return false;
    }

    // Browse menu support
    juce::Array<juce::TreeViewItem*> getDeepestLevelItems() override;
    juce::String getParentCategoryForItem(juce::TreeViewItem* item) override;

    // Find and mark item as downloading/not downloading
    void setItemDownloading(const juce::String& packageName, bool downloading);

    // Draw glow effects for downloading items (called by FilteredTreeView)
    void drawDownloadGlowEffects(juce::Graphics& g);

private:
    AudioPluginAudioProcessor& processor;

    // Category definitions
    struct CategoryEntry
    {
        juce::String displayName;
        juce::File directory;
        bool isStandardCategory = true; // False for custom user directories
    };

    juce::Array<CategoryEntry> categories;

    // Remote repository support
    struct RemoteRepository
    {
        juce::String name;
        juce::String indexUrl;
        std::vector<ReaPackIndexParser::JsfxEntry> entries;
        bool isLoaded = false;
    };

    juce::Array<RemoteRepository> remoteRepositories;
    std::unique_ptr<ReaPackDownloader> downloader;

    // Pinned packages (prevent updates)
    juce::StringArray pinnedPackages;

    // Cached package version info
    struct CachedPackageInfo
    {
        juce::String packageName;
        juce::String version;   // Display version (e.g., "1.0.2")
        juce::String timestamp; // Timestamp for version comparison
    };

    juce::Array<CachedPackageInfo> cachedPackages;

    // Animation timer callback
    void timerCallback() override;

    // Track if any items are downloading (for timer management)
    int activeDownloads = 0;

    // Track destruction to prevent callbacks from accessing destroyed object
    std::atomic<bool> isDestroyed{false};

    // Scan a directory for .jsfx files
    void scanDirectory(JsfxPluginTreeItem* parentItem, const juce::File& directory, bool recursive);

    // Add remote repository entries to tree item
    void addRemoteEntries(JsfxPluginTreeItem* repoItem, const std::vector<ReaPackIndexParser::JsfxEntry>& entries);

    // Persistence for remote repositories
    void loadSavedRepositories();
    void saveRepositories();
    void loadPinnedPackages();
    void savePinnedPackages();

    // First-run default repository initialization
    void fetchAndAddDefaultRepository(const juce::String& url);

    // Cached package management
    void updateCachedPackageInfo(
        const juce::String& packageName,
        const juce::String& version,
        const juce::String& timestamp
    );
    juce::String getCachedPackageTimestamp(const juce::String& packageName) const;

    // Helper to collect selected items recursively
    void collectSelectedPluginItems(juce::Array<JsfxPluginTreeItem*>& items, juce::TreeViewItem* item);

    RemoteRepository* findRepositoryByUrl(const juce::String& url);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxPluginTreeView)
};
