#include "LibraryManager.h"

LibraryManager::LibraryManager(juce::ValueTree stateTree, const juce::Identifier& propertyName)
    : parentState(stateTree)
{
    // Find or create the Libraries tree
    librariesTree = parentState.getChildWithName(propertyName);
    if (!librariesTree.isValid())
    {
        librariesTree = juce::ValueTree(propertyName);
        parentState.appendChild(librariesTree, nullptr);
    }
}

void LibraryManager::loadSubLibrary(
    const juce::String& libraryName,
    const juce::Array<juce::File>& files,
    ParserFunction parser,
    bool clearExisting
)
{
    if (!parser)
    {
        DBG("LibraryManager::loadSubLibrary - No parser provided!");
        return;
    }

    DBG("LibraryManager::loadSubLibrary - Loading " << files.size() << " files into '" << libraryName << "'");

    auto subLibrary = getOrCreateSubLibrary(libraryName);

    if (clearExisting)
    {
        DBG("  Clearing existing children (" << subLibrary.getNumChildren() << ")");
        subLibrary.removeAllChildren(nullptr);
    }

    // Parse each file and add to sub-library
    int filesAdded = 0;
    for (const auto& file : files)
    {
        if (!file.existsAsFile())
        {
            DBG("  File doesn't exist: " << file.getFullPathName());
            continue;
        }

        DBG("  Parsing: " << file.getFileName());
        auto parsed = parser(file);

        if (parsed.isValid())
        {
            DBG("    Valid tree returned, type: " << parsed.getType().toString());
            DBG("    Has " << parsed.getNumChildren() << " children");

            // Add the parsed tree itself (e.g., PresetFile with its children)
            subLibrary.appendChild(parsed, nullptr);
            filesAdded++;
        }
        else
        {
            DBG("    Parser returned invalid tree!");
        }
    }

    DBG("  Added " << filesAdded << " items to sub-library");
}

void LibraryManager::scanAndLoadSubLibrary(
    const juce::String& libraryName,
    const juce::StringArray& directories,
    const juce::String& filePattern,
    ParserFunction parser,
    bool recursive,
    bool clearExisting
)
{
    DBG("LibraryManager::scanAndLoadSubLibrary - Library: " << libraryName);
    DBG("  Scanning " << directories.size() << " directories");
    DBG("  File pattern: " << filePattern);
    DBG("  Recursive: " << (recursive ? "YES" : "NO"));

    juce::Array<juce::File> files;

    for (const auto& dir : directories)
    {
        juce::File directory(dir);
        DBG("  Checking directory: " << dir);

        if (!directory.isDirectory())
        {
            DBG("    Not a directory!");
            continue;
        }

        auto foundFiles = directory.findChildFiles(juce::File::findFiles, recursive, filePattern);

        DBG("    Found " << foundFiles.size() << " files matching pattern");
        files.addArray(foundFiles);
    }

    DBG("  Total files to parse: " << files.size());
    loadSubLibrary(libraryName, files, parser, clearExisting);

    auto subLib = getSubLibrary(libraryName);
    if (subLib.isValid())
        DBG("  Sub-library now has " << subLib.getNumChildren() << " children");
}

juce::ValueTree LibraryManager::getSubLibrary(const juce::String& libraryName) const
{
    for (int i = 0; i < librariesTree.getNumChildren(); ++i)
    {
        auto child = librariesTree.getChild(i);
        if (child.getProperty("name").toString() == libraryName)
            return child;
    }
    return {};
}

bool LibraryManager::hasSubLibrary(const juce::String& libraryName) const
{
    return getSubLibrary(libraryName).isValid();
}

void LibraryManager::clearSubLibrary(const juce::String& libraryName)
{
    auto subLib = getSubLibrary(libraryName);
    if (subLib.isValid())
        librariesTree.removeChild(subLib, nullptr);
}

void LibraryManager::clear()
{
    librariesTree.removeAllChildren(nullptr);
}

juce::ValueTree LibraryManager::getOrCreateSubLibrary(const juce::String& libraryName)
{
    auto existing = getSubLibrary(libraryName);
    if (existing.isValid())
        return existing;

    // Create new sub-library
    juce::ValueTree subLib("SubLibrary");
    subLib.setProperty("name", libraryName, nullptr);
    librariesTree.appendChild(subLib, nullptr);
    return subLib;
}
