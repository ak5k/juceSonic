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
