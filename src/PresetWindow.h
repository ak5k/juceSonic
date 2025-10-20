#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PresetTreeView.h"
#include "WindowWithButtonRow.h"
#include <Config.h>

// Forward declaration
class AudioPluginAudioProcessor;

/**
 * @brief Window for managing JSFX presets
 *
 * Features:
 * - Browse presets in hierarchical tree view (Directory > File > Bank > Preset)
 * - Export selected presets
 * - Delete selected presets
 * - Configure preset search directories
 */
class PresetWindow : public WindowWithButtonRow
{
public:
    explicit PresetWindow(AudioPluginAudioProcessor& proc);
    ~PresetWindow() override;

    void visibilityChanged() override;

    /**
     * @brief Configure whether to show management buttons (Export, Delete, etc.)
     * Set to false when embedding in editor for minimal UI
     */
    void setShowManagementButtons(bool show);

    /**
     * @brief Get direct access to the tree view for integration
     */
    PresetTreeView& getTreeView()
    {
        return presetTreeView;
    }

    /**
     * @brief Hide or show the status label
     */
    void setStatusLabelVisible(bool visible)
    {
        getStatusLabel().setVisible(visible);
    }

    /**
     * @brief Refresh preset list from APVTS data
     */
    void refreshPresetList();

    /**
     * @brief Callback invoked when a preset is selected/loaded
     * Parameters: (bankName, presetName, presetData)
     */
    std::function<void(const juce::String&, const juce::String&, const juce::String&)> onPresetSelected;

protected:
    // Override from WindowWithButtonRow
    juce::Component* getMainComponent() override
    {
        return &presetTreeView;
    }

private:
    void exportSelectedPresets();
    void deleteSelectedPresets();
    void showDirectoryEditor();
    void updateButtonsForSelection();
    void handlePresetTreeItemSelected(juce::TreeViewItem* item);
    void saveCurrentPreset();
    void resetToDefaults();
    void setAsDefaultPreset();

    // Helper to recursively collect all preset items from selected items
    void
    collectPresetsRecursively(juce::Array<PresetTreeItem*>& presets, const juce::Array<PresetTreeItem*>& selectedItems);

    // Load/save directory paths from persistent storage
    juce::StringArray getPresetDirectories() const;
    void setPresetDirectories(const juce::StringArray& directories);

    AudioPluginAudioProcessor& processor;

    // UI Components - button pointers managed by base class ButtonRowComponent
    juce::TextButton* exportButton = nullptr;
    juce::TextButton* deleteButton = nullptr;
    juce::TextButton* saveButton = nullptr;
    juce::TextButton* defaultButton = nullptr;
    juce::TextButton* setDefaultButton = nullptr;
    juce::TextButton* directoriesButton = nullptr;
    juce::TextButton* refreshButton = nullptr;

    PresetTreeView presetTreeView;

    bool showManagementButtons = true;

    // Track currently selected preset for delete operations
    juce::String currentPresetBankName;
    juce::String currentPresetName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetWindow)
};

/**
 * @brief Dialog for editing preset search directories
 */
class PresetDirectoryEditor : public juce::Component
{
public:
    PresetDirectoryEditor(
        const juce::StringArray& currentDirectories,
        std::function<void(const juce::StringArray&)> onSave
    );
    ~PresetDirectoryEditor() override;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetDirectoryEditor)
};
