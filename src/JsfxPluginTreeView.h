#pragma once

#include "SearchableTreeView.h"
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
        Category, // Root category (User, Local, Remote, REAPER)
        Plugin    // Individual JSFX plugin file
    };

    JsfxPluginTreeItem(
        const juce::String& name,
        ItemType t,
        const juce::File& file = juce::File(),
        JsfxPluginTreeView* view = nullptr
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

    // Accessors
    ItemType getType() const
    {
        return type;
    }

    const juce::File& getFile() const
    {
        return pluginFile;
    }

private:
    juce::String itemName;
    ItemType type;
    juce::File pluginFile; // For Plugin items
    JsfxPluginTreeView* pluginTreeView = nullptr;
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
class JsfxPluginTreeView : public SearchableTreeView
{
public:
    explicit JsfxPluginTreeView(AudioPluginAudioProcessor& proc);
    ~JsfxPluginTreeView() override;

    // Callbacks
    std::function<void()> onSelectionChangedCallback;

    // Load plugins from directory paths
    void loadPlugins(const juce::StringArray& directoryPaths);

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

private:
    AudioPluginAudioProcessor& processor;

    // Category definitions
    struct CategoryEntry
    {
        juce::String displayName;
        juce::File directory;
    };

    juce::Array<CategoryEntry> categories;

    // Load plugin to processor
    void loadPlugin(const juce::File& pluginFile);

    // Scan a directory for .jsfx files
    void scanDirectory(JsfxPluginTreeItem* parentItem, const juce::File& directory, bool recursive);

    // Helper to collect selected items recursively
    void collectSelectedPluginItems(juce::Array<JsfxPluginTreeItem*>& items, juce::TreeViewItem* item);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxPluginTreeView)
};
