#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RepositoryManager.h"

// Forward declaration
class RepositoryTreeItem;

/**
 * @brief Window for managing JSFX repositories
 *
 * Allows users to:
 * - Add/remove repository URLs
 * - Browse available JSFX from repositories
 * - Install JSFX packages
 */
class RepositoryWindow
    : public juce::Component
    , private juce::Timer
{
public:
    explicit RepositoryWindow(RepositoryManager& repoManager);
    ~RepositoryWindow() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

private:
    // (TreeView-based UI; ListBoxModel methods removed)

    // Timer for refreshing repository list
    void timerCallback() override;

    void refreshRepositoryList();
    void refreshPackageList();
    void checkForVersionMismatches();
    void installSelectedPackage();
    void installAllPackages();
    void uninstallSelectedPackage();
    void uninstallAllPackages();
    void proceedWithInstallation();
    void showRepositoryEditor();
    void updateInstallAllButtonText();

public:
    void updateButtonsForSelection();
    juce::Array<RepositoryTreeItem*> getSelectedRepoItems();

    // Multi-item operations (all operations work on selections)
    void installFromTreeItems(const juce::Array<RepositoryTreeItem*>& items);
    void uninstallFromTreeItems(const juce::Array<RepositoryTreeItem*>& items);
    void pinAllFromTreeItems(const juce::Array<RepositoryTreeItem*>& items);
    void unpinAllFromTreeItems(const juce::Array<RepositoryTreeItem*>& items);
    void ignoreAllFromTreeItems(const juce::Array<RepositoryTreeItem*>& items);
    void unignoreAllFromTreeItems(const juce::Array<RepositoryTreeItem*>& items);

    // Legacy single-item operations (now wrappers for multi-item operations)
    void installPackage(const RepositoryManager::JSFXPackage& package);
    void uninstallPackage(const RepositoryManager::JSFXPackage& package);
    void installFromTreeItem(RepositoryTreeItem* item);
    void uninstallFromTreeItem(RepositoryTreeItem* item);
    void togglePackagePinned(const RepositoryManager::JSFXPackage& package);
    void togglePackageIgnored(const RepositoryManager::JSFXPackage& package);
    void pinAllFromTreeItem(RepositoryTreeItem* item);
    void unpinAllFromTreeItem(RepositoryTreeItem* item);
    void ignoreAllFromTreeItem(RepositoryTreeItem* item);
    void unignoreAllFromTreeItem(RepositoryTreeItem* item);

private:
    RepositoryManager& repositoryManager;

    // UI Components
    juce::TextButton manageReposButton;
    juce::TextButton refreshButton;

    juce::TreeView repoTree;
    std::unique_ptr<class RepositoryTreeItem> rootItem;
    juce::TextButton installButton;
    juce::TextButton installAllButton;
    juce::TextButton cancelButton;
    juce::Label statusLabel;

    // Data
    std::vector<RepositoryManager::Repository> repositories;
    std::vector<RepositoryManager::JSFXPackage> allPackages;
    bool isLoading = false;
    bool isInstalling = false;

    // Helper to collect selected tree items
    void collectSelectedRepoItems(juce::Array<RepositoryTreeItem*>& items, RepositoryTreeItem* item);

    // Helper methods for refactored operations
    struct PackageCollectionResult
    {
        std::vector<RepositoryManager::JSFXPackage> packages;
        int skippedPinned = 0;
        int skippedIgnored = 0;

        juce::String getSkipMessage() const;
    };

    PackageCollectionResult collectPackagesFromTreeItem(RepositoryTreeItem* item, bool installedOnly);
    void setButtonsEnabled(bool enabled);
    void
    showConfirmationDialog(const juce::String& title, const juce::String& message, std::function<void()> onConfirm);
    void executeBatchOperation(
        const std::vector<RepositoryManager::JSFXPackage>& packages,
        const juce::String& skipMessage,
        bool isInstall
    );

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RepositoryWindow)
};

/**
 * @brief Dialog for editing repository URLs
 */
class RepositoryEditorDialog : public juce::Component
{
public:
    RepositoryEditorDialog(RepositoryManager& repoManager, std::function<void()> onClose);
    ~RepositoryEditorDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void saveAndClose();
    void cancel();

    RepositoryManager& repositoryManager;
    std::function<void()> closeCallback;

    juce::TextEditor instructionsLabel;
    juce::TextEditor repositoryEditor;
    juce::TextButton saveButton;
    juce::TextButton cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RepositoryEditorDialog)
};
