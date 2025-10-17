#include "ReaperPresetParser.h"

juce::ValueTree ReaperPresetParser::parseFile(const juce::File& file)
{
    DBG("ReaperPresetParser::parseFile - " << file.getFileName());

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
        while (
            nameStart < len
            && (data[nameStart] == ' ' || data[nameStart] == '\t' || data[nameStart] == '\r' || data[nameStart] == '\n')
        )
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

        // Find closing > for this library (bracket matching, skip quoted sections)
        int depth = 1;
        int searchPos = nameEnd + 1;
        int libraryEnd = -1;

        for (int i = searchPos; i < len && depth > 0; i++)
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

juce::ValueTree ReaperPresetParser::parseRPLFile(const juce::File& file)
{
    return parseFile(file);
}
