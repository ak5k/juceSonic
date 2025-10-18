#include "PresetLoader.h"

PresetLoader::PresetLoader(juce::AudioProcessorValueTreeState& apvts_)
    : juce::Thread("PresetLoader")
    , apvts(apvts_)
    , converter(std::make_unique<ReaperPresetConverter>())
{
    // Start the background thread (will wait for requests)
    startThread(juce::Thread::Priority::low);
}

PresetLoader::~PresetLoader()
{
    // Signal thread to stop and wait for it to finish
    signalThreadShouldExit();
    notify();
    stopThread(5000);
}

void PresetLoader::requestRefresh(const juce::String& jsfxPath)
{
    DBG("PresetLoader::requestRefresh called with path: " << (jsfxPath.isEmpty() ? "<empty>" : jsfxPath));

    // Update pending path atomically
    {
        juce::ScopedLock lock(pathLock);
        pendingJsfxPath = jsfxPath;
    }

    // Signal that a refresh is requested and wake up the thread
    refreshRequested.store(true);
    notify();
}

int PresetLoader::getLoadedFileCount() const
{
    auto presetsNode = apvts.state.getChildWithName("presets");
    if (!presetsNode.isValid())
        return 0;
    return presetsNode.getNumChildren();
}

int PresetLoader::getLoadedBankCount() const
{
    auto presetsNode = apvts.state.getChildWithName("presets");
    if (!presetsNode.isValid())
        return 0;

    int totalBanks = 0;
    for (int i = 0; i < presetsNode.getNumChildren(); ++i)
    {
        auto fileNode = presetsNode.getChild(i);
        totalBanks += fileNode.getNumChildren();
    }
    return totalBanks;
}

void PresetLoader::run()
{
    while (!threadShouldExit())
    {
        // Wait for a refresh request
        wait(-1);

        // Check if we should exit
        if (threadShouldExit())
            break;

        // Check if there's a refresh request
        if (refreshRequested.exchange(false))
            loadPresetsInBackground();
    }
}

void PresetLoader::loadPresetsInBackground()
{
    isCurrentlyLoading.store(true);

    juce::String currentJsfxPath;
    {
        juce::ScopedLock lock(pathLock);
        currentJsfxPath = pendingJsfxPath;
    }

    DBG("PresetLoader: Starting background load for: " << currentJsfxPath);

    // Create new preset tree
    juce::ValueTree newPresetsTree("presets");

    // If no JSFX loaded, just clear presets
    if (currentJsfxPath.isEmpty())
    {
        DBG("PresetLoader: No JSFX loaded, clearing presets");
        juce::MessageManager::callAsync([this, newPresetsTree]() mutable
                                        { updatePresetsInState(std::move(newPresetsTree)); });
        isCurrentlyLoading.store(false);
        return;
    }

    juce::File jsfxFile(currentJsfxPath);
    juce::String jsfxName = jsfxFile.getFileNameWithoutExtension();

    DBG("PresetLoader: Searching for presets for JSFX: " << jsfxName);

    // Phase 1: Collect all file paths (without holding any locks)
    auto presetFiles = findPresetFiles(jsfxFile, jsfxName);

    DBG("PresetLoader: Found " << presetFiles.size() << " preset files to load");

    // Check if we should exit before starting I/O
    if (threadShouldExit())
    {
        isCurrentlyLoading.store(false);
        return;
    }

    // Phase 2: Load and parse files (uses global file I/O)
    // The ReaperPresetConverter internally handles file reading
    for (const auto& file : presetFiles)
    {
        // Check if we should exit
        if (threadShouldExit())
        {
            isCurrentlyLoading.store(false);
            return;
        }

        // Check if a new refresh was requested (cancel current operation)
        if (refreshRequested.load())
        {
            DBG("PresetLoader: New refresh requested, cancelling current load");
            isCurrentlyLoading.store(false);
            return;
        }

        DBG("PresetLoader: Loading preset file: " << file.getFileName());

        auto fileNode = converter->convertFileToTree(file);
        if (fileNode.isValid())
        {
            DBG("PresetLoader:   File has " << fileNode.getNumChildren() << " banks");
            newPresetsTree.appendChild(fileNode, nullptr);
        }
        else
        {
            DBG("PresetLoader:   Failed to load file");
        }
    }

    DBG("PresetLoader: Finished loading, total files: "
        << newPresetsTree.getNumChildren()
        << ", scheduling state update on message thread");

    // Phase 3: Update APVTS state on message thread (atomic)
    juce::MessageManager::callAsync([this, newPresetsTree]() mutable
                                    { updatePresetsInState(std::move(newPresetsTree)); });

    isCurrentlyLoading.store(false);
}

juce::Array<juce::File> PresetLoader::findPresetFiles(const juce::File& jsfxFile, const juce::String& jsfxName)
{
    juce::Array<juce::File> presetFiles;

    // 1. Check same directory as loaded JSFX file (any .rpl files)
    juce::File jsfxDirectory = jsfxFile.getParentDirectory();
    DBG("PresetLoader:   Checking JSFX directory: " << jsfxDirectory.getFullPathName());

    if (jsfxDirectory.exists())
    {
        auto localRplFiles = jsfxDirectory.findChildFiles(juce::File::findFiles, false, "*.rpl");
        DBG("PresetLoader:   Found " << localRplFiles.size() << " .rpl files in JSFX directory");

        for (const auto& file : localRplFiles)
        {
            presetFiles.add(file);
            DBG("PresetLoader:     Adding: " << file.getFileName());
        }
    }

    // 2. Add presets from persistent storage directories
    auto dirString = apvts.state.getProperty("presetDirectories", "").toString();

    if (dirString.isNotEmpty())
    {
        juce::StringArray directories;
        directories.addLines(dirString);

        DBG("PresetLoader:   Checking " << directories.size() << " configured directories");

        for (const auto& dirPath : directories)
        {
            juce::File dir(dirPath);
            if (dir.exists() && dir.isDirectory())
            {
                auto storedPresets = dir.findChildFiles(juce::File::findFiles, false, "*.rpl");
                DBG("PresetLoader:     Found " << storedPresets.size() << " presets in " << dirPath);

                for (const auto& file : storedPresets)
                    presetFiles.add(file);
            }
        }
    }

    // 3. Check REAPER Effects directory (recursive, filtered by JSFX name)
    auto reaperEffectsPath = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                 .getChildFile("REAPER")
                                 .getChildFile("Effects");

    DBG("PresetLoader:   Checking REAPER Effects path: " << reaperEffectsPath.getFullPathName());

    if (reaperEffectsPath.exists())
    {
        auto rplFiles = reaperEffectsPath.findChildFiles(juce::File::findFiles, true, "*.rpl");
        DBG("PresetLoader:   Found " << rplFiles.size() << " .rpl files in REAPER Effects (recursive)");

        // Filter to only include files matching current JSFX name
        for (const auto& file : rplFiles)
        {
            juce::String filename = file.getFileNameWithoutExtension();
            // Match if filename equals JSFX name (case-insensitive)
            if (filename.equalsIgnoreCase(jsfxName))
            {
                presetFiles.add(file);
                DBG("PresetLoader:     Matched: " << file.getFileName());
            }
        }
    }

    return presetFiles;
}

void PresetLoader::updatePresetsInState(juce::ValueTree newPresetsTree)
{
    // This runs on the message thread - safe to modify APVTS
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    DBG("PresetLoader: Updating APVTS state on message thread");

    // Remove old presets node if it exists
    auto oldPresetsNode = apvts.state.getChildWithName("presets");
    if (oldPresetsNode.isValid())
    {
        apvts.state.removeChild(oldPresetsNode, nullptr);
        DBG("PresetLoader:   Removed old presets node");
    }

    // Add new presets node
    apvts.state.appendChild(newPresetsTree, nullptr);

    DBG("PresetLoader:   Added new presets node with " << newPresetsTree.getNumChildren() << " files");

    // Count total banks for logging
    int totalBanks = 0;
    for (int i = 0; i < newPresetsTree.getNumChildren(); ++i)
        totalBanks += newPresetsTree.getChild(i).getNumChildren();

    DBG("PresetLoader:   Total banks: " << totalBanks);
    DBG("PresetLoader: State update complete");
}
