#pragma once

#include "PresetConverter.h"

/**
 * Converter for Reaper preset files (.rpl).
 *
 * Parses JSFX preset files and creates a hierarchical ValueTree structure:
 *
 * Structure:
 *   PresetFile (type="PresetFile", one per .rpl file)
 *     - property: "name" (file name without extension)
 *     - property: "file" (full file path)
 *     - PresetBank (type="PresetBank", one per <REAPER_PRESET_LIBRARY> tag)
 *       - property: "name" (library name from tag)
 *       - Preset (type="Preset", multiple children)
 *         - property: "name" (preset name)
 *         - property: "data" (base64 encoded preset data)
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
class ReaperPresetConverter : public PresetConverter
{
public:
    ReaperPresetConverter() = default;
    ~ReaperPresetConverter() override = default;

    juce::ValueTree convertFileToTree(const juce::File& file) override
    {
        if (!canConvert(file))
            return juce::ValueTree();

        DBG("ReaperPresetConverter::convertFileToTree - " << file.getFileName());

        if (!file.existsAsFile())
        {
            DBG("  File doesn't exist!");
            return {};
        }

        auto content = file.loadFileAsString();
        if (content.isEmpty())
        {
            DBG("  File is empty!");
            return {};
        }

        DBG("  File size: " << content.length() << " bytes");

        // Parse all banks from this .rpl file
        juce::ValueTree presetFile("PresetFile");
        presetFile.setProperty("name", file.getFileNameWithoutExtension(), nullptr);
        presetFile.setProperty("file", file.getFullPathName(), nullptr);

        const char* data = content.toRawUTF8();
        int len = content.length();
        int pos = 0;

        int banksFound = 0;

        // Find all <REAPER_PRESET_LIBRARY> blocks in this file
        while (pos < len)
        {
            // Find start of library tag
            int libStart = content.indexOf(pos, "<REAPER_PRESET_LIBRARY");
            if (libStart == -1)
                break;

            // Extract library name - first non-whitespace char after tag is the delimiter
            int nameStart = libStart + 22; // strlen("<REAPER_PRESET_LIBRARY")
            while (nameStart < len
                   && (data[nameStart] == ' '
                       || data[nameStart] == '\t'
                       || data[nameStart] == '\r'
                       || data[nameStart] == '\n'))
                nameStart++;

            if (nameStart >= len)
                break;

            char quoteChar = data[nameStart]; // First char is the delimiter
            int nameEnd = nameStart + 1;
            while (nameEnd < len && data[nameEnd] != quoteChar)
                nameEnd++;

            if (nameEnd >= len)
                break;

            juce::String libraryName = content.substring(nameStart + 1, nameEnd);

            // Remove "JS: " prefix if present
            if (libraryName.startsWith("JS: "))
                libraryName = libraryName.substring(4);

            DBG("  Found library: " << libraryName);

            // Find closing > for the opening tag first
            int openTagEnd = nameEnd + 1;
            while (openTagEnd < len && data[openTagEnd] != '>')
                openTagEnd++;

            if (openTagEnd >= len)
                break;

            // Find closing > for this library (bracket matching, skip quoted sections)
            int depth = 1;
            int libraryEnd = -1;

            for (int i = openTagEnd + 1; i < len && depth > 0; i++)
            {
                char c = data[i];
                // Skip over quoted sections
                if (c == '`' || c == '"' || c == '\'')
                {
                    char quote = c;
                    i++;
                    while (i < len && data[i] != quote)
                        i++;
                    continue;
                }

                if (c == '<')
                    depth++;
                else if (c == '>')
                {
                    depth--;
                    if (depth == 0)
                    {
                        libraryEnd = i;
                        break;
                    }
                }
            }

            if (libraryEnd == -1)
                break;

            // Create bank for this library
            juce::ValueTree bank("PresetBank");
            bank.setProperty("name", libraryName, nullptr);

            // Parse presets in this library
            int presetPos = nameEnd + 1;

            while (presetPos < libraryEnd)
            {
                // Find <PRESET
                int presetStart = presetPos;
                while (presetStart < libraryEnd
                       && (presetStart + 7 >= libraryEnd || strncmp(data + presetStart, "<PRESET", 7) != 0))
                    presetStart++;

                if (presetStart >= libraryEnd)
                    break;

                // Find opening delimiter - first non-whitespace char after <PRESET
                int pNameStart = presetStart + 7;
                while (pNameStart < libraryEnd
                       && (data[pNameStart] == ' '
                           || data[pNameStart] == '\t'
                           || data[pNameStart] == '\r'
                           || data[pNameStart] == '\n'))
                    pNameStart++;

                if (pNameStart >= libraryEnd)
                    break;

                char pQuoteChar = data[pNameStart];
                int pNameEnd = pNameStart + 1;
                while (pNameEnd < libraryEnd && data[pNameEnd] != pQuoteChar)
                    pNameEnd++;

                if (pNameEnd >= libraryEnd)
                    break;

                juce::String presetName = content.substring(pNameStart + 1, pNameEnd);

                // Find closing > for this preset
                int pDepth = 1;
                int presetEnd = -1;

                for (int i = pNameEnd + 1; i < libraryEnd && pDepth > 0; i++)
                {
                    char c = data[i];
                    // Skip quoted sections
                    if (c == '`' || c == '"' || c == '\'')
                    {
                        char quote = c;
                        i++;
                        while (i < libraryEnd && data[i] != quote)
                            i++;
                        continue;
                    }

                    if (c == '<')
                        pDepth++;
                    else if (c == '>')
                    {
                        pDepth--;
                        if (pDepth == 0)
                        {
                            presetEnd = i;
                            break;
                        }
                    }
                }

                if (presetEnd == -1)
                    break;

                // Extract preset data (base64 between name and closing >)
                juce::String presetData = content.substring(pNameEnd + 1, presetEnd).trim();

                // Add preset to bank
                if (presetName.isNotEmpty() && presetData.isNotEmpty())
                {
                    juce::ValueTree preset("Preset");
                    preset.setProperty("name", presetName, nullptr);
                    preset.setProperty("data", presetData, nullptr);
                    bank.appendChild(preset, nullptr);
                }

                presetPos = presetEnd + 1;
            }

            // Add bank to preset file if it has presets
            if (bank.getNumChildren() > 0)
            {
                DBG("    Bank '" << libraryName << "' has " << bank.getNumChildren() << " presets");
                presetFile.appendChild(bank, nullptr);
                banksFound++;
            }
            else
            {
                DBG("    Bank '" << libraryName << "' has no presets - skipping");
            }

            pos = libraryEnd + 1;
        }

        DBG("  Total banks parsed: " << banksFound);
        DBG("  Preset file has " << presetFile.getNumChildren() << " banks");

        return presetFile;
    }

    bool convertTreeToFile(const juce::ValueTree& tree, const juce::File& targetFile) override
    {
        if (!tree.isValid() || tree.getType().toString() != "PresetFile")
        {
            DBG("convertTreeToFile: Invalid tree structure");
            return false;
        }

        juce::String output;

        // Write each bank
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            auto bank = tree.getChild(i);
            if (bank.getType().toString() != "PresetBank")
                continue;

            auto bankName = bank.getProperty("name").toString();

            // Write library header with backtick delimiter
            output += "<REAPER_PRESET_LIBRARY `" + bankName + "`\n";

            // Write each preset in the bank
            for (int j = 0; j < bank.getNumChildren(); ++j)
            {
                auto preset = bank.getChild(j);
                if (preset.getType().toString() != "Preset")
                    continue;

                auto presetName = preset.getProperty("name").toString();
                auto presetData = preset.getProperty("data").toString();

                // Write preset with backtick delimiter
                output += "  <PRESET `" + presetName + "`\n";

                // Write preset data (already base64 encoded)
                // Add proper indentation
                auto lines = juce::StringArray::fromLines(presetData);
                for (const auto& line : lines)
                    if (line.isNotEmpty())
                        output += "    " + line + "\n";

                output += "  >\n";
            }

            output += ">\n";
        }

        // Write to file
        if (targetFile.replaceWithText(output))
        {
            DBG("Successfully wrote preset file: " << targetFile.getFullPathName());
            return true;
        }

        DBG("Failed to write preset file: " << targetFile.getFullPathName());
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

    /**
     * Find and extract a specific preset by name from a preset file.
     * This is a utility method that can be used without creating a full ValueTree.
     *
     * @param file The .rpl preset file to search
     * @param presetName The name of the preset to find
     * @return The base64 encoded preset data, or empty string if not found
     */
    static juce::String findPresetByName(const juce::File& file, const juce::String& presetName)
    {
        if (!file.existsAsFile())
            return {};

        auto content = file.loadFileAsString();
        if (content.isEmpty())
            return {};

        const char* data = content.toRawUTF8();
        int len = content.length();

        // Find the preset with the given name
        int presetPos = 0;
        while (presetPos < len)
        {
            // Find <PRESET
            int presetStart = presetPos;
            while (presetStart < len && (presetStart + 7 >= len || strncmp(data + presetStart, "<PRESET", 7) != 0))
                presetStart++;

            if (presetStart >= len)
                break;

            // Find preset name - first non-whitespace char after <PRESET
            int pNameStart = presetStart + 7;
            while (pNameStart < len
                   && (data[pNameStart] == ' '
                       || data[pNameStart] == '\t'
                       || data[pNameStart] == '\r'
                       || data[pNameStart] == '\n'))
                pNameStart++;

            if (pNameStart >= len)
                break;

            char pQuoteChar = data[pNameStart];
            int pNameEnd = pNameStart + 1;
            while (pNameEnd < len && data[pNameEnd] != pQuoteChar)
                pNameEnd++;

            if (pNameEnd >= len)
                break;

            juce::String foundPresetName = content.substring(pNameStart + 1, pNameEnd);

            // Check if this is the preset we're looking for
            if (foundPresetName == presetName)
            {
                // Find closing > for this preset using bracket matching
                int pDepth = 1;
                int presetEnd = -1;

                for (int i = pNameEnd + 1; i < len && pDepth > 0; i++)
                {
                    char c = data[i];
                    // Skip quoted sections
                    if (c == '`' || c == '"' || c == '\'')
                    {
                        char quote = c;
                        i++;
                        while (i < len && data[i] != quote)
                            i++;
                        continue;
                    }

                    if (c == '<')
                        pDepth++;
                    else if (c == '>')
                    {
                        pDepth--;
                        if (pDepth == 0)
                        {
                            presetEnd = i;
                            break;
                        }
                    }
                }

                if (presetEnd > 0)
                {
                    // Extract preset data (everything between name and closing >)
                    return content.substring(pNameEnd + 1, presetEnd).trim();
                }
            }

            presetPos = pNameEnd + 1;
        }

        return {}; // Preset not found
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReaperPresetConverter)
};