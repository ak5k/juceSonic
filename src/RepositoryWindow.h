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
    , private juce::ListBoxModel
    , private juce::Timer
{
public:
    explicit RepositoryWindow(RepositoryManager& repoManager);
    ~RepositoryWindow() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

private:
    // ListBoxModel implementation
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

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

    RepositoryManager& repositoryManager;

    // UI Components
    juce::Label repositoriesLabel;
    juce::TextButton manageReposButton;
    juce::TextButton refreshButton;

    juce::Label packagesLabel;
    juce::ListBox packageList;
    juce::TextButton installButton;
    juce::TextButton installAllButton;
    juce::Label statusLabel;

    // Data
    std::vector<RepositoryManager::Repository> repositories;
    std::vector<RepositoryManager::JSFXPackage> allPackages;
    int selectedPackageIndex = -1;
    bool isLoading = false;

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

    juce::Label instructionsLabel;
    juce::TextEditor repositoryEditor;
    juce::TextButton saveButton;
    juce::TextButton cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RepositoryEditorDialog)
};
