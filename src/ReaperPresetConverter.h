#pragma once

#include "PresetConverter.h"
#include "ReaperPresetParser.h"

/**
 * Concrete converter for Reaper preset files (.rpl).
 *
 * Adapts the ReaperPresetParser to the PresetConverter interface,
 * allowing LibraryManager to work with Reaper presets without
 * knowing the implementation details.
 */
class ReaperPresetConverter : public PresetConverter
{
public:
    ReaperPresetConverter() = default;
    ~ReaperPresetConverter() override = default;

    juce::ValueTree convertFileToTree(const juce::File& file) override
    {
        if (!canConvert(file))
            return juce::ValueTree();

        ReaperPresetParser parser;
        return parser.parseFile(file);
    }

    bool convertTreeToFile(const juce::ValueTree& tree, const juce::File& targetFile) override
    {
        // TODO: Implement writing Reaper preset files
        // For now, this is read-only
        juce::ignoreUnused(tree, targetFile);
        return false;
    }

    bool canConvert(const juce::File& file) const override
    {
        if (!file.existsAsFile())
            return false;

        // Reaper preset files have .rpl extension (case-insensitive)
        return file.getFileExtension().equalsIgnoreCase(".rpl");
    }

    juce::StringArray getSupportedExtensions() const override
    {
        return {"*.rpl"};
    }

    juce::String getFormatName() const override
    {
        return "Reaper Preset";
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReaperPresetConverter)
};
