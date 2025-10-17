#include "PresetParser.h"

juce::ValueTree PresetParser::parseFile(const juce::File& file)
{
    auto result = parseRPLFile(file);

    // Create ValueTree structure
    juce::ValueTree bank("PresetBank");
    bank.setProperty("name", result.libraryName, nullptr);
    bank.setProperty("file", file.getFullPathName(), nullptr);

    for (const auto& presetVar : result.presets)
    {
        if (auto* obj = presetVar.getDynamicObject())
        {
            juce::ValueTree preset("Preset");
            preset.setProperty("name", obj->getProperty("name"), nullptr);
            preset.setProperty("data", obj->getProperty("data"), nullptr);
            bank.appendChild(preset, nullptr);
        }
    }

    return bank;
}

PresetParser::ParseResult PresetParser::parseRPLFile(const juce::File& file)
{
    ParseResult result;

    if (!file.existsAsFile())
        return result;

    juce::String content = file.loadFileAsString();
    juce::StringArray lines = juce::StringArray::fromLines(content);

    juce::String currentPresetName;
    juce::String currentPresetData;
    bool inPresetBlock = false;

    for (const auto& line : lines)
    {
        juce::String trimmed = line.trim();

        // Library name
        if (trimmed.startsWith("<LIBNAME"))
        {
            int startQuote = trimmed.indexOf(0, "\"");
            int endQuote = trimmed.lastIndexOf("\"");
            if (startQuote >= 0 && endQuote > startQuote)
                result.libraryName = trimmed.substring(startQuote + 1, endQuote);
            continue;
        }

        // Preset start
        if (trimmed.startsWith("<PRESET"))
        {
            // Extract delimiter (first non-whitespace char after PRESET)
            int spaceAfterTag = trimmed.indexOf(" ");
            if (spaceAfterTag < 0)
                continue;

            juce::String afterTag = trimmed.substring(spaceAfterTag + 1).trim();
            if (afterTag.isEmpty())
                continue;

            juce::juce_wchar delimiter = afterTag[0];

            // Find matching closing delimiter
            int endDelim = afterTag.indexOf(1, juce::String::charToString(delimiter));
            if (endDelim > 0)
            {
                currentPresetName = afterTag.substring(1, endDelim);
                inPresetBlock = true;
                currentPresetData.clear();
            }
            continue;
        }

        // Preset end
        if (trimmed == ">" && inPresetBlock)
        {
            if (currentPresetName.isNotEmpty() && currentPresetData.isNotEmpty())
            {
                auto preset = new juce::DynamicObject();
                preset->setProperty("name", currentPresetName);
                preset->setProperty("data", currentPresetData);
                result.presets.add(juce::var(preset));
            }

            currentPresetName.clear();
            currentPresetData.clear();
            inPresetBlock = false;
            continue;
        }

        // Preset data
        if (inPresetBlock && trimmed.isNotEmpty())
            currentPresetData += trimmed;
    }

    return result;
}
