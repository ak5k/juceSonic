#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <vector>

/**
 * Manages JSFX presets from .rpl (REAPER Preset Library) files.
 *
 * .rpl file format:
 * <REAPER_PRESET_LIBRARY `JS: EffectName`>
 *   <PRESET `PresetName`>
 *     base64_encoded_binary_data
 *   >
 * >
 */
class PresetManager
{
public:
    struct Preset
    {
        juce::String name;
        juce::String libraryName; // JSFX effect name
        juce::String filePath;    // Source .rpl file path
        juce::String data;        // Base64-encoded parameter data
    };

    struct PresetBank
    {
        juce::String libraryName; // JSFX effect name from library tag
        juce::String filePath;
        std::vector<Preset> presets;
    };

    PresetManager();
    ~PresetManager();

    // Scan directories for .rpl files and load all presets
    void scanDirectories(const juce::Array<juce::File>& directories);

    // Get all presets for a specific JSFX effect (by name match)
    std::vector<Preset> getPresetsForEffect(const juce::String& effectName) const;

    // Get a specific preset by name (returns first match)
    const Preset* getPreset(const juce::String& presetName) const;

    // Get all available preset names (optionally filtered by effect name)
    juce::StringArray getAllPresetNames(const juce::String& effectNameFilter = {}) const;

    // Get all loaded banks
    const std::vector<PresetBank>& getBanks() const
    {
        return banks;
    }

    // Clear all loaded presets
    void clear();

private:
    std::vector<PresetBank> banks;

    // Parse a single .rpl file
    bool parsePresetFile(const juce::File& file);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};
