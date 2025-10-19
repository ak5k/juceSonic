#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RepositoryManager.h"
#include "RepositoryTreeView.h"
#include "JuceSonicLookAndFeel.h"

// Forward declaration
class AudioPluginAudioProcessor;

/**
 * @brief Window for managing JSFX repositories
 *
 * Allows users to:
 * - Add/remove repository URLs
 * - Browse available JSFX from repositories
 * - Install JSFX packages
 *
 * Self-contained component that manages its own RepositoryManager instance
 */
class RepositoryWindow
    : public juce::Component
    , private juce::Timer
{
public:
    explicit RepositoryWindow(AudioPluginAudioProcessor& processor);
    ~RepositoryWindow() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

private:
    // Timer for refreshing repository list
    void timerCallback() override;

    void refreshRepositoryList();
    void checkForVersionMismatches();
    void installSelectedPackage();
    void installAllPackages();
    void uninstallSelectedPackage();
    void uninstallAllPackages();
    void proceedWithInstallation();
    void showRepositoryEditor();
    void updateInstallAllButtonText();
    void updateButtonsForSelection();

    // Package operations (delegated to RepositoryTreeView)
    void installPackage(const RepositoryManager::JSFXPackage& package);
    void uninstallPackage(const RepositoryManager::JSFXPackage& package);
    void batchInstallPackages(const std::vector<RepositoryManager::JSFXPackage>& packages);
    void batchUninstallPackages(const std::vector<RepositoryManager::JSFXPackage>& packages);

private:
    AudioPluginAudioProcessor& processor;
    std::unique_ptr<RepositoryManager> repositoryManager;

    // UI Components
    juce::TextButton manageReposButton;
    juce::TextButton refreshButton;

    RepositoryTreeView repositoryTreeView;

    juce::TextButton installButton;
    juce::TextButton installAllButton;
    juce::TextButton cancelButton;
    juce::Label statusLabel;

    // Data
    std::vector<RepositoryManager::Repository> repositories;
    std::vector<RepositoryManager::JSFXPackage> allPackages;
    bool isLoading = false;
    bool isInstalling = false;

    juce::SharedResourcePointer<SharedJuceSonicLookAndFeel> sharedLookAndFeel;

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

    juce::SharedResourcePointer<SharedJuceSonicLookAndFeel> sharedLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RepositoryEditorDialog)
};
