#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PresetTreeView.h"
#include "JuceSonicLookAndFeel.h"

// Forward declaration
class AudioPluginAudioProcessor;

/**
 * @brief Window for managing JSFX presets
 *
 * Features:
 * - Browse presets in hierarchical tree view (Directory > File > Bank > Preset)
 * - Import .rpl preset files
 * - Export selected presets
 * - Delete selected presets
 * - Configure preset search directories
 */
class PresetWindow : public juce::Component
{
public:
    explicit PresetWindow(AudioPluginAudioProcessor& proc);
    ~PresetWindow() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    /**
     * @brief Configure whether to show management buttons (Import, Export, etc.)
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

private:
    void refreshPresetList();
    void importPresetFile();
    void exportSelectedPresets();
    void deleteSelectedPresets();
    void showDirectoryEditor();
    void updateButtonsForSelection();

    // Load/save directory paths from persistent storage
    juce::StringArray getPresetDirectories() const;
    void setPresetDirectories(const juce::StringArray& directories);

    AudioPluginAudioProcessor& processor;

    // UI Components
    juce::TextButton importButton{"Import"};
    juce::TextButton exportButton{"Export"};
    juce::TextButton deleteButton{"Delete"};
    juce::TextButton directoriesButton{"Directories"};
    juce::TextButton refreshButton{"Refresh"};

    PresetTreeView presetTreeView;

    juce::Label statusLabel;

    bool showManagementButtons = true;

    juce::SharedResourcePointer<SharedJuceSonicLookAndFeel> sharedLookAndFeel;

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
