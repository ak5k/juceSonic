#pragma once

#include "Parser.h"

/**
 * @brief Parser for REAPER .rpl (REAPER Preset Library) files
 *
 * Parses JSFX preset files and creates a hierarchical ValueTree structure:
 *
 * Structure:
 *   JSFXEffect (type="JSFXEffect")
 *     - property: "name" (JSFX effect name, e.g., "delay")
 *     - PresetFile (type="PresetFile", one per .rpl file)
 *       - property: "name" (file name without extension)
 *       - property: "file" (full file path)
 *       - PresetBank (type="PresetBank", one per <REAPER_PRESET_LIBRARY> tag)
 *         - property: "name" (library name from tag)
 *         - Preset (type="Preset", multiple children)
 *           - property: "name" (preset name)
 *           - property: "data" (base64 encoded preset data)
 *
 * .rpl file format example:
 * <REAPER_PRESET_LIBRARY `JS: delay`
 *   <PRESET `Short Delay`
 *     BASE64_DATA_HERE
 *   >
 *   <PRESET `Long Delay`
 *     BASE64_DATA_HERE
 *   >
 * >
 */
class ReaperPresetParser : public Parser
{
public:
    ReaperPresetParser() = default;
    ~ReaperPresetParser() override = default;

    /**
     * @brief Parse a REAPER .rpl file
     * @param file The .rpl file to parse
     * @return ValueTree with JSFXEffect structure, or invalid tree on failure
     */
    juce::ValueTree parseFile(const juce::File& file) override;

    juce::String getFileExtension() const override
    {
        return "rpl";
    }

private:
    /**
     * @brief Internal parsing logic for .rpl file format
     * @param file The file to parse
     * @return ValueTree containing the parsed preset banks
     */
    juce::ValueTree parseRPLFile(const juce::File& file);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReaperPresetParser)
};
