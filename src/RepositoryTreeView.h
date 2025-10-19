#pragma once

#include "SearchableTreeView.h"
#include "RepositoryManager.h"

// Forward declarations
class RepositoryTreeView;

/**
 * @brief Tree item for repository browser
 * Displays repositories, categories, and packages with installation status
 */
class RepositoryTreeItem : public SearchableTreeItem
{
public:
    enum class ItemType
    {
        Index,    // Repository
        Category, // Category within repository
        Package,  // Individual JSFX package
        Metadata  // Additional info items
    };

    RepositoryTreeItem(
        const juce::String& name,
        ItemType t,
        const RepositoryManager::JSFXPackage* pkg = nullptr,
        RepositoryTreeView* view = nullptr
    );

    // SearchableTreeItem overrides
    juce::String getName() const override
    {
        return itemName;
    }

    void paintItem(juce::Graphics& g, int width, int height) override;
    void paintOpenCloseButton(
        juce::Graphics& g,
        const juce::Rectangle<float>& area,
        juce::Colour backgroundColour,
        bool isMouseOver
    ) override;
    bool mightContainSubItems() override;
    bool canBeSelected() const override;
    void itemClicked(const juce::MouseEvent& e) override;
    void itemSelectionChanged(bool isNowSelected) override;

    // Accessors
    ItemType getType() const
    {
        return type;
    }

    const RepositoryManager::JSFXPackage* getPackage() const
    {
        return package;
    }

    void setRepositoryManager(RepositoryManager* mgr)
    {
        repositoryManager = mgr;
    }

    void setRepositoryTreeView(RepositoryTreeView* view)
    {
        repositoryTreeView = view;
    }

private:
    juce::String itemName;
    ItemType type;
    const RepositoryManager::JSFXPackage* package;
    RepositoryManager* repositoryManager = nullptr;
    RepositoryTreeView* repositoryTreeView = nullptr;
};

/**
 * @brief Searchable tree view for JSFX repositories
 *
 * Extends SearchableTreeView with repository-specific functionality:
 * - Displays installation status badges
 * - Right-click context menus for install/uninstall/pin/ignore
 * - Hierarchical display of repositories → categories → packages
 */
class RepositoryTreeView : public SearchableTreeView
{
public:
    explicit RepositoryTreeView(RepositoryManager& repoManager);
    ~RepositoryTreeView() override;

    // Callbacks for install/uninstall operations (set by RepositoryWindow)
    std::function<void(const RepositoryManager::JSFXPackage&)> onInstallPackage;
    std::function<void(const RepositoryManager::JSFXPackage&)> onUninstallPackage;
    std::function<void(const std::vector<RepositoryManager::JSFXPackage>&)> onBatchInstallPackages;
    std::function<void(const std::vector<RepositoryManager::JSFXPackage>&)> onBatchUninstallPackages;
    std::function<void()> onSelectionChangedCallback;

    // Refresh repository data
    void refreshRepositories()
    {
        refreshTree();
    }

    // Set repository data (called from RepositoryWindow after async load)
    void setRepositories(const std::vector<RepositoryManager::Repository>& repos)
    {
        repositories = repos;
    }

    void setAllPackages(const std::vector<RepositoryManager::JSFXPackage>& packages)
    {
        allPackages = packages;
    }

    // Multi-item operations
    void installFromTreeItems(const juce::Array<juce::TreeViewItem*>& items);
    void uninstallFromTreeItems(const juce::Array<juce::TreeViewItem*>& items);
    void pinAllFromTreeItems(const juce::Array<juce::TreeViewItem*>& items);
    void unpinAllFromTreeItems(const juce::Array<juce::TreeViewItem*>& items);
    void ignoreAllFromTreeItems(const juce::Array<juce::TreeViewItem*>& items);
    void unignoreAllFromTreeItems(const juce::Array<juce::TreeViewItem*>& items);

    // Single-item operations (wrappers)
    void installPackage(const RepositoryManager::JSFXPackage& package);
    void uninstallPackage(const RepositoryManager::JSFXPackage& package);
    void togglePackagePinned(const RepositoryManager::JSFXPackage& package);
    void togglePackageIgnored(const RepositoryManager::JSFXPackage& package);

    // Get repository manager
    RepositoryManager& getRepositoryManager()
    {
        return repositoryManager;
    }

    // Get current packages list
    const std::vector<RepositoryManager::JSFXPackage>& getAllPackages() const
    {
        return allPackages;
    }

    // Helper to collect selected repository tree items (used by RepositoryTreeItem)
    juce::Array<RepositoryTreeItem*> getSelectedRepoItems();

    // SearchableTreeView overrides
    std::unique_ptr<juce::TreeViewItem> createRootItem() override;
    void onSelectionChanged() override;
    void onEnterKeyPressed(juce::TreeViewItem* selectedItem) override;

    juce::String getSearchPlaceholder() const override
    {
        return "Type to search packages...";
    }

    juce::Array<std::pair<juce::String, juce::String>> getMetadataForItem(juce::TreeViewItem* item) override;

    bool shouldIncludeInSearch(juce::TreeViewItem* item) override;

private:
    RepositoryManager& repositoryManager;

    // Data
    std::vector<RepositoryManager::Repository> repositories;
    std::vector<RepositoryManager::JSFXPackage> allPackages;

    // Helper to collect selected repository tree items
    void collectSelectedRepoItems(juce::Array<RepositoryTreeItem*>& items, juce::TreeViewItem* item);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RepositoryTreeView)
};
