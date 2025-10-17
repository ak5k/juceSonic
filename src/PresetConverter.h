#pragma once

#include <juce_data_structures/juce_data_structures.h>

/**
 * Abstract interface for converting preset files to/from ValueTree format.
 *
 * This is the Strategy pattern interface that allows LibraryBrowser to work
 * with different preset formats without knowing their implementation details.
 *
 * Each concrete converter (ReaperPresetConverter, etc.) implements this interface
 * to handle format-specific parsing and serialization.
 */
class PresetConverter
{
public:
    virtual ~PresetConverter() = default;

    /**
     * Convert a preset file to ValueTree format.
     *
     * @param file The preset file to parse
     * @return ValueTree representing the preset data, or invalid tree on failure
     *
     * Expected tree structure:
     * PresetFile (type: "PresetFile", name: filename)
     *   └─ PresetBank (type: "PresetBank", name: bank name)
     *      └─ Preset (type: "Preset", name: preset name, data: base64 encoded)
     */
    virtual juce::ValueTree convertFileToTree(const juce::File& file) = 0;

    /**
     * Convert ValueTree format back to a preset file.
     *
     * @param tree The ValueTree containing preset data
     * @param targetFile The file to write to
     * @return true on success, false on failure
     */
    virtual bool convertTreeToFile(const juce::ValueTree& tree, const juce::File& targetFile) = 0;

    /**
     * Check if this converter can handle the given file.
     *
     * @param file The file to check
     * @return true if this converter supports this file format
     */
    virtual bool canConvert(const juce::File& file) const = 0;

    /**
     * Get supported file extensions (e.g., "*.rpl", "*.fxp").
     *
     * @return Array of file pattern strings for file choosers
     */
    virtual juce::StringArray getSupportedExtensions() const = 0;

    /**
     * Get a human-readable name for this preset format.
     *
     * @return Format name (e.g., "Reaper Preset", "VST Preset")
     */
    virtual juce::String getFormatName() const = 0;
};
