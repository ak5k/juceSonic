#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RepositoryManager.h"

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
    void installSelectedPackage();
    void installAllPackages();
    void uninstallSelectedPackage();
    void uninstallAllPackages();
    void proceedWithInstallation();
    void showRepositoryEditor();
    void updateInstallAllButtonText();

public:
    void updateButtonsForSelection();

    // Context menu operations
    void installPackage(const RepositoryManager::JSFXPackage& package);
    void uninstallPackage(const RepositoryManager::JSFXPackage& package);
    void installFromTreeItem(class RepositoryTreeItem* item);
    void uninstallFromTreeItem(class RepositoryTreeItem* item);
    void togglePackagePinned(const RepositoryManager::JSFXPackage& package);
    void togglePackageIgnored(const RepositoryManager::JSFXPackage& package);

private:
    RepositoryManager& repositoryManager;

    // UI Components
    juce::TextButton manageReposButton;
    juce::TextButton refreshButton;

    juce::TreeView repoTree;
    std::unique_ptr<class RepositoryTreeItem> rootItem;
    juce::TextButton installButton;
    juce::TextButton installAllButton;
    juce::Label statusLabel;

    // Data
    std::vector<RepositoryManager::Repository> repositories;
    std::vector<RepositoryManager::JSFXPackage> allPackages;
    bool isLoading = false;

    // Helper to collect selected tree items
    void collectSelectedRepoItems(juce::Array<RepositoryTreeItem*>& items, RepositoryTreeItem* item);
    juce::Array<RepositoryTreeItem*> getSelectedRepoItems();

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
