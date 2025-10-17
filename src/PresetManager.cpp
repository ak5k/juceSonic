#include "PresetManager.h"

PresetManager::PresetManager()
{
}

PresetManager::~PresetManager()
{
    clear();
}

void PresetManager::scanDirectories(const juce::Array<juce::File>& directories)
{
    clear();

    DBG("PresetManager: Starting scan of " << directories.size() << " directories");

    for (const auto& dir : directories)
    {
        DBG("PresetManager: Checking directory: " << dir.getFullPathName());

        if (!dir.exists())
        {
            DBG("  Directory does not exist!");
            continue;
        }

        if (!dir.isDirectory())
        {
            DBG("  Path is not a directory!");
            continue;
        }

        // Search recursively for .rpl files
        auto rplFiles = dir.findChildFiles(juce::File::findFiles, true, "*.rpl");
        DBG("  Found " << rplFiles.size() << " .rpl files");

        for (const auto& file : rplFiles)
        {
            DBG("  Parsing: " << file.getFileName());
            parsePresetFile(file);
        }
    }

    DBG("PresetManager: Loaded " + juce::String(banks.size()) + " preset banks");
}

std::vector<PresetManager::Preset> PresetManager::getPresetsForEffect(const juce::String& effectName) const
{
    std::vector<Preset> result;

    DBG("PresetManager::getPresetsForEffect: searching for '" << effectName << "' in " << banks.size() << " banks");

    for (const auto& bank : banks)
    {
        DBG("  Bank: '" << bank.libraryName << "' (" << bank.presets.size() << " presets)");

        // Match by library name (contains check, case-insensitive)
        if (bank.libraryName.containsIgnoreCase(effectName) || effectName.isEmpty())
        {
            DBG("    MATCH! Adding " << bank.presets.size() << " presets");
            result.insert(result.end(), bank.presets.begin(), bank.presets.end());
        }
    }

    DBG("PresetManager::getPresetsForEffect: returning " << result.size() << " presets");
    return result;
}

const PresetManager::Preset* PresetManager::getPreset(const juce::String& presetName) const
{
    for (const auto& bank : banks)
    {
        for (const auto& preset : bank.presets)
            if (preset.name.equalsIgnoreCase(presetName))
                return &preset;
    }

    return nullptr;
}

juce::StringArray PresetManager::getAllPresetNames(const juce::String& effectNameFilter) const
{
    juce::StringArray names;

    for (const auto& bank : banks)
    {
        if (effectNameFilter.isEmpty() || bank.libraryName.containsIgnoreCase(effectNameFilter))
            for (const auto& preset : bank.presets)
                names.add(preset.name);
    }

    return names;
}

void PresetManager::clear()
{
    banks.clear();
}

bool PresetManager::parsePresetFile(const juce::File& file)
{
    auto content = file.loadFileAsString();
    DBG("  File size: " << content.length() << " bytes");

    if (content.isEmpty())
    {
        DBG("  ERROR: File is empty!");
        return false;
    }

    int totalBanks = 0;
    const char* data = content.toRawUTF8();
    int len = content.length();
    int pos = 0;

    // Find all <REAPER_PRESET_LIBRARY `name` ... > blocks
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

        char quoteChar = data[nameStart]; // First char is the delimiter (must match closing)
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

        // Find the closing > for this library (fast bracket matching, skip quoted sections)
        int depth = 1;
        int searchPos = nameEnd + 1;
        int libraryEnd = -1;

        for (int i = searchPos; i < len && depth > 0; i++)
        {
            char c = data[i];
            // Skip over quoted sections (backticks, double quotes, or single quotes)
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
        {
            DBG("  ERROR: Could not find closing > for library!");
            break;
        }

        // Parse presets in this library
        PresetBank bank;
        bank.filePath = file.getFullPathName();
        bank.libraryName = libraryName;

        int presetPos = nameEnd + 1;
        int presetCount = 0;

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

            char pQuoteChar = data[pNameStart]; // First char is the delimiter (must match closing)
            int pNameEnd = pNameStart + 1;
            while (pNameEnd < libraryEnd && data[pNameEnd] != pQuoteChar)
                pNameEnd++;
            if (pNameEnd >= libraryEnd)
                break;

            juce::String presetName = content.substring(pNameStart + 1, pNameEnd);
            presetCount++;

            // Find closing > for this preset
            int pDepth = 1;
            int presetEnd = -1;

            for (int i = pNameEnd + 1; i < libraryEnd && pDepth > 0; i++)
            {
                char c = data[i];
                // Skip quoted sections (backticks, double quotes, or single quotes)
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
            {
                DBG("  ERROR: Could not find closing > for preset!");
                break;
            }

            // Extract preset data (base64 between name and closing >)
            juce::String presetData = content.substring(pNameEnd + 1, presetEnd).trim();

            Preset preset;
            preset.libraryName = libraryName;
            preset.filePath = bank.filePath;
            preset.name = presetName;
            preset.data = presetData;

            if (preset.name.isNotEmpty() && preset.data.isNotEmpty())
                bank.presets.push_back(preset);

            presetPos = presetEnd + 1;
        }

        DBG("  Parsed " << presetCount << " PRESET tags, " << bank.presets.size() << " valid presets");

        if (!bank.presets.empty())
        {
            banks.push_back(bank);
            totalBanks++;
            DBG("  SUCCESS: Loaded bank '" << bank.libraryName << "' with " << bank.presets.size() << " presets");
        }

        // Advance past this library to continue searching for more libraries
        pos = libraryEnd + 1;
    }

    if (totalBanks > 0)
    {
        DBG("  Total: Loaded " << totalBanks << " banks from file");
        return true;
    }

    DBG("  WARNING: No valid presets found in file");
    return false;
}
