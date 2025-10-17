#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

// Forward declaration
class AudioPluginAudioProcessor;

/**
 * PresetManager handles all preset operations including:
 * - Loading/saving presets from/to disk
 * - Managing default presets per-JSFX
 * - Importing/exporting presets
 * - Managing user presets in persistent storage
 *
 * Presets are stored in the following structure:
 * - Global persistent storage: <AppData>/juceSonic/data/
 *   - /author-name/
 *     - /jsfx-filename/
 *       - default.rpl (default preset for that JSFX)
 *       - imported presets (.rpl files)
 *
 * The author name is parsed from the "author:" tag in the JSFX file.
 * If no author is found, "Unknown" is used as the default author folder.
 *
 * When a JSFX is loaded, the manager checks for a default preset and applies it
 * after the JSFX is initialized.
 */
class PresetManager
{
public:
    PresetManager(AudioPluginAudioProcessor& processor);
    ~PresetManager();

    /**
     * Reset to default state - loads the JSFX's initial state.
     * Shows confirmation dialog before resetting.
     * @param parentComponent Parent for modal dialog
     */
    void resetToDefault(juce::Component* parentComponent);

    /**
     * Save current state as a new preset.
     * Shows dialog to get bank name (defaults to "User") and preset name.
     * @param parentComponent Parent for modal dialog
     */
    void saveAs(juce::Component* parentComponent);

    /**
     * Set current state as default preset for this JSFX.
     * Saves to /jsfx-filename/default.rpl in global persistent storage.
     * @param parentComponent Parent for modal dialog (for confirmation)
     */
    void setAsDefault(juce::Component* parentComponent);

    /**
     * Import preset file from disk.
     * Shows file chooser to select .rpl file, then copies it to
     * /jsfx-filename/ directory in persistent storage.
     * @param parentComponent Parent for modal dialog
     */
    void importPreset(juce::Component* parentComponent);

    /**
     * Export current preset to disk.
     * Shows dialog with three options: Export All, Export Bank, or Export Preset.
     * @param parentComponent Parent for modal dialog
     */
    void exportPreset(juce::Component* parentComponent);

    /**
     * Delete current preset.
     * Shows confirmation dialog before deleting.
     * Searches all available preset files and removes the first match.
     * @param parentComponent Parent for modal dialog
     * @param bankName Bank name containing the preset
     * @param presetName Name of preset to delete
     */
    void deletePreset(juce::Component* parentComponent, const juce::String& bankName, const juce::String& presetName);

    /**
     * Apply default preset if one exists for current JSFX.
     * Called automatically when loading a JSFX.
     * @return true if default preset was found and applied
     */
    bool applyDefaultPresetIfExists();

    /**
     * Get the persistent storage directory for the current JSFX.
     * Returns /jsfx-filename/ directory in app data.
     */
    juce::File getJsfxStorageDirectory() const;

    /**
     * Get the default preset file for the current JSFX.
     * Returns /jsfx-filename/default.rpl path.
     */
    juce::File getDefaultPresetFile() const;

    /**
     * Check if a default preset exists for the current JSFX.
     */
    bool hasDefaultPreset() const;

    /**
     * Get current preset state as base64 encoded string.
     * This captures all parameter values in JSFX format.
     */
    juce::String getCurrentPresetAsBase64() const;

    /**
     * Save preset data to an .rpl file.
     * @param file Destination file
     * @param bankName Bank name for the preset library
     * @param presetName Name of the preset
     * @param base64Data Base64 encoded preset data
     * @return true if save was successful
     */
    bool savePresetToFile(
        const juce::File& file,
        const juce::String& bankName,
        const juce::String& presetName,
        const juce::String& base64Data
    );

    /**
     * Set callback to be called when presets are modified (saved, imported, deleted).
     * This allows the UI to refresh the preset list.
     */
    void setOnPresetsChangedCallback(std::function<void()> callback)
    {
        onPresetsChanged = callback;
    }

private:
    AudioPluginAudioProcessor& processor;
    juce::ApplicationProperties appProperties;
    std::function<void()> onPresetsChanged;

    /**
     * Initialize application properties for persistent storage.
     */
    void initializeProperties();

    /**
     * Get the root directory for all preset storage.
     * Returns <AppData>/juceSonic/
     */
    juce::File getPresetRootDirectory() const;

    /**
     * Sanitize filename for safe file system use.
     */
    juce::String sanitizeFilename(const juce::String& name) const;

    /**
     * Export all presets from storage directory.
     */
    void exportAllPresets(juce::Component* parentComponent);

    /**
     * Show dialog to choose and export a bank.
     */
    void exportBankDialog(juce::Component* parentComponent);

    /**
     * Show dialog to export current state as a single preset.
     */
    void exportPresetDialog(juce::Component* parentComponent);

    /**
     * Combine multiple preset files into one.
     */
    bool combinePresetFiles(const juce::Array<juce::File>& files, const juce::File& outputFile);

    /**
     * Remove a specific preset from the content string.
     * @param content The .rpl file content to modify (passed by reference)
     * @param bankName The bank containing the preset
     * @param presetName The preset to remove
     * @return true if the preset was found and removed
     */
    bool removePresetFromContent(juce::String& content, const juce::String& bankName, const juce::String& presetName);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};
