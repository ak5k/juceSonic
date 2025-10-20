#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "JsfxPluginTreeView.h"
#include "WindowWithButtonRow.h"
#include <Config.h>

// Forward declaration
class AudioPluginAudioProcessor;

/**
 * @brief Window for managing JSFX plugins
 *
 * Features:
 * - Browse JSFX plugins in hierarchical tree view (Category > Plugin)
 * - Load JSFX plugin files
 * - Delete selected plugins
 * - Configure plugin search directories
 * - Categories: User, Local, Remote, and REAPER
 */
class JsfxPluginWindow : public WindowWithButtonRow
{
public:
    explicit JsfxPluginWindow(AudioPluginAudioProcessor& proc);
    ~JsfxPluginWindow() override;

    void visibilityChanged() override;

    /**
     * @brief Configure whether to show management buttons
     * Set to false when embedding in editor for minimal UI
     */
    void setShowManagementButtons(bool show);

    /**
     * @brief Get direct access to the tree view for integration
     */
    JsfxPluginTreeView& getTreeView()
    {
        return pluginTreeView;
    }

    /**
     * @brief Refresh plugin list from directories
     */
    void refreshPluginList();

    /**
     * @brief Callback invoked when a plugin is selected/loaded
     * Parameters: (pluginPath)
     */
    std::function<void(const juce::String&)> onPluginSelected;

protected:
    // Override from WindowWithButtonRow
    juce::Component* getMainComponent() override
    {
        return &pluginTreeView;
    }

private:
    void deleteSelectedPlugins();
    void showDirectoryEditor();
    void showRepositoryEditor();
    void updateAllRemotePlugins();
    void updateButtonsForSelection();
    void handlePluginTreeItemSelected(juce::TreeViewItem* item);

    // Load/save directory paths from persistent storage
    juce::StringArray getPluginDirectories() const;
    void setPluginDirectories(const juce::StringArray& directories);

    AudioPluginAudioProcessor& processor;

    // UI Components - button pointers managed by base class ButtonRowComponent
    juce::TextButton* loadButton = nullptr;
    juce::TextButton* deleteButton = nullptr;
    juce::TextButton* directoriesButton = nullptr;
    juce::TextButton* repositoriesButton = nullptr;
    juce::TextButton* updateAllButton = nullptr;
    juce::TextButton* refreshButton = nullptr;

    JsfxPluginTreeView pluginTreeView;

    bool showManagementButtons = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxPluginWindow)
};

/**
 * @brief Dialog for editing JSFX plugin search directories
 */
class JsfxPluginDirectoryEditor : public juce::Component
{
public:
    JsfxPluginDirectoryEditor(
        const juce::StringArray& currentDirectories,
        std::function<void(const juce::StringArray&)> onSave
    );
    ~JsfxPluginDirectoryEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void saveAndClose();
    void cancel();

    std::function<void(const juce::StringArray&)> saveCallback;

    juce::TextEditor instructionsLabel;
    juce::TextEditor directoryEditor;
    juce::TextButton saveButton{"Save"};
    juce::TextButton cancelButton{"Cancel"};

    juce::SharedResourcePointer<SharedJuceSonicLookAndFeel> sharedLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxPluginDirectoryEditor)
};

/**
 * @brief Dialog for editing remote ReaPack repository URLs
 */
class JsfxRepositoryEditor : public juce::Component
{
public:
    JsfxRepositoryEditor(JsfxPluginTreeView& treeView, std::function<void()> onSave);
    ~JsfxRepositoryEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.originalComponent == &repositoryList)
            updateButtonStates();
    }

private:
    void saveAndClose();
    void cancel();
    void addRepository();
    void removeSelectedRepository();
    void fetchRepositoryName();
    void onUrlChanged();
    void updateButtonStates();

    JsfxPluginTreeView& pluginTreeView;
    std::function<void()> saveCallback;

    juce::TextEditor instructionsLabel;
    juce::ListBox repositoryList;
    juce::TextEditor nameEditor;
    juce::TextEditor urlEditor;
    juce::TextButton addButton{"Add"};
    juce::TextButton removeButton{"Remove"};
    juce::TextButton saveButton{"Save"};
    juce::TextButton cancelButton{"Cancel"};

    juce::SharedResourcePointer<SharedJuceSonicLookAndFeel> sharedLookAndFeel;

    // ListBox model
    class RepositoryListModel : public juce::ListBoxModel
    {
    public:
        struct RepositoryEntry
        {
            juce::String name;
            juce::String url;
        };

        std::vector<RepositoryEntry> repositories;

        int getNumRows() override
        {
            return (int)repositories.size();
        }

        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
        {
            if (rowIsSelected)
                g.fillAll(juce::Colours::lightblue);

            if (rowNumber < (int)repositories.size())
            {
                g.setColour(juce::Colours::white);
                g.setFont(12.0f);

                auto& repo = repositories[rowNumber];
                g.drawText(repo.name, 5, 0, width - 10, height / 2, juce::Justification::left, true);

                g.setColour(juce::Colours::grey);
                g.setFont(10.0f);
                g.drawText(repo.url, 5, height / 2, width - 10, height / 2, juce::Justification::left, true);
            }
        }
    };

    RepositoryListModel listModel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxRepositoryEditor)
};
