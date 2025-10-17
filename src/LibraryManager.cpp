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

void LibraryManager::prepareLibrary(const juce::String& libraryName, std::unique_ptr<PresetConverter> converter)
{
    if (!converter)
    {
        DBG("LibraryManager::prepareLibrary - Null converter provided!");
        return;
    }

    DBG("LibraryManager::prepareLibrary - Library: '" << libraryName << "', Format: " << converter->getFormatName());

    // Store converter for this library
    converters[libraryName] = std::move(converter);

    // Ensure library node exists
    getOrCreateLibrary(libraryName);
}

int LibraryManager::loadLibrary(
    const juce::String& libraryName,
    const juce::String& directoryPath,
    bool recursive,
    bool clearExisting
)
{
    juce::StringArray paths;
    paths.add(directoryPath);
    return loadLibrary(libraryName, paths, recursive, clearExisting);
}

int LibraryManager::loadLibrary(
    const juce::String& libraryName,
    const juce::StringArray& directoryPaths,
    bool recursive,
    bool clearExisting
)
{
    auto* converter = getConverter(libraryName);
    if (!converter)
    {
        DBG("LibraryManager::loadLibrary - No converter set for library '" << libraryName << "'");
        DBG("  Call prepareLibrary() first!");
        return 0;
    }

    DBG("LibraryManager::loadLibrary - Library: '" << libraryName << "'");
    DBG("  Scanning " << directoryPaths.size() << " directories");
    DBG("  Format: " << converter->getFormatName());
    DBG("  Recursive: " << (recursive ? "YES" : "NO"));

    auto library = getOrCreateLibrary(libraryName);

    if (clearExisting)
    {
        DBG("  Clearing existing children (" << library.getNumChildren() << ")");
        library.removeAllChildren(nullptr);
    }

    // Scan all directories for files
    juce::Array<juce::File> allFiles;
    for (const auto& dirPath : directoryPaths)
    {
        auto files = scanFiles(dirPath, converter, recursive);
        DBG("  Found " << files.size() << " files in: " << dirPath);
        allFiles.addArray(files);
    }

    DBG("  Total files to convert: " << allFiles.size());

    // Convert each file and add to library
    int filesAdded = 0;
    for (const auto& file : allFiles)
    {
        DBG("  Converting: " << file.getFileName());
        auto converted = converter->convertFileToTree(file);

        if (converted.isValid())
        {
            DBG("    Valid tree returned, type: " << converted.getType().toString());
            DBG("    Has " << converted.getNumChildren() << " children");

            // Add the converted tree itself (e.g., PresetFile with its children)
            library.appendChild(converted, nullptr);
            filesAdded++;
        }
        else
        {
            DBG("    Converter returned invalid tree!");
        }
    }

    DBG("  Added " << filesAdded << " items to library '" << libraryName << "'");
    return filesAdded;
}

juce::ValueTree LibraryManager::getLibrary(const juce::String& libraryName) const
{
    for (int i = 0; i < librariesTree.getNumChildren(); ++i)
    {
        auto child = librariesTree.getChild(i);
        if (child.getProperty("name").toString() == libraryName)
            return child;
    }
    return {};
}

bool LibraryManager::hasLibrary(const juce::String& libraryName) const
{
    return getLibrary(libraryName).isValid();
}

void LibraryManager::clearLibrary(const juce::String& libraryName)
{
    auto lib = getLibrary(libraryName);
    if (lib.isValid())
        librariesTree.removeChild(lib, nullptr);
}

void LibraryManager::clear()
{
    librariesTree.removeAllChildren(nullptr);
    converters.clear();
}

PresetConverter* LibraryManager::getConverter(const juce::String& libraryName) const
{
    auto it = converters.find(libraryName);
    if (it != converters.end())
        return it->second.get();
    return nullptr;
}

juce::ValueTree LibraryManager::getOrCreateLibrary(const juce::String& libraryName)
{
    auto existing = getLibrary(libraryName);
    if (existing.isValid())
        return existing;

    // Create new library
    juce::ValueTree lib("Library");
    lib.setProperty("name", libraryName, nullptr);
    librariesTree.appendChild(lib, nullptr);
    return lib;
}

juce::Array<juce::File>
LibraryManager::scanFiles(const juce::String& directoryPath, PresetConverter* converter, bool recursive)
{
    juce::Array<juce::File> result;

    juce::File directory(directoryPath);
    if (!directory.isDirectory())
    {
        DBG("    Not a directory: " << directoryPath);
        return result;
    }

    // Get all files in directory
    auto allFiles = directory.findChildFiles(
        juce::File::findFiles,
        recursive,
        "*" // Get all files, we'll filter with canConvert()
    );

    // Filter files that the converter can handle
    for (const auto& file : allFiles)
        if (converter->canConvert(file))
            result.add(file);

    return result;
}
