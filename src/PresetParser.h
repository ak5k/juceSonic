#pragma once

#include <juce_data_structures/juce_data_structures.h>

/**
 * @brief Parser for REAPER .rpl preset files
 *
 * Converts .rpl file format into a JUCE ValueTree structure.
 * Supports flexible delimiter detection for preset definitions.
 */
class PresetParser
{
public:
    PresetParser() = default;
    ~PresetParser() = default;

    /**
     * @brief Parse a .rpl file and return presets as ValueTree
     * @param file The .rpl file to parse
     * @return ValueTree with structure:
     *   Root (type: "PresetBank")
     *     - property: "name" (library name)
     *     - children: Preset nodes (type: "Preset")
     *       - property: "name" (preset name)
     *       - property: "data" (base64 data)
     */
    juce::ValueTree parseFile(const juce::File& file);

private:
    struct ParseResult
    {
        juce::String libraryName;
        juce::Array<juce::var> presets; // Array of objects with "name" and "data"
    };

    ParseResult parseRPLFile(const juce::File& file);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetParser)
};
