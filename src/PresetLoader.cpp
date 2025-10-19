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

    // Create new preset tree
    juce::ValueTree newPresetsTree("presets");

    // If no JSFX loaded, just clear presets
    if (currentJsfxPath.isEmpty())
    {
        juce::MessageManager::callAsync([this, newPresetsTree]() mutable
                                        { updatePresetsInState(std::move(newPresetsTree)); });
        isCurrentlyLoading.store(false);
        return;
    }

    juce::File jsfxFile(currentJsfxPath);
    juce::String jsfxName = jsfxFile.getFileNameWithoutExtension();

    // Phase 1: Collect all file paths (without holding any locks)
    auto presetFiles = findPresetFiles(jsfxFile, jsfxName);

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
            isCurrentlyLoading.store(false);
            return;
        }

        auto fileNode = converter->convertFileToTree(file);
        if (fileNode.isValid())
        {
            newPresetsTree.appendChild(fileNode, nullptr);
        }
        else
        {
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

    // 0. Check user presets directory first (highest priority)
    // User presets go to: <appdata>/juceSonic/data/user/<jsfx-filename>/
    auto userPresetsDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile(PluginConstants::ApplicationName)
                              .getChildFile(PluginConstants::DataDirectoryName)
                              .getChildFile(PluginConstants::UserPresetsDirectoryName)
                              .getChildFile(jsfxName);

    if (userPresetsDir.exists() && userPresetsDir.isDirectory())
    {
        auto userRplFiles = userPresetsDir.findChildFiles(juce::File::findFiles, false, "*.rpl");
        for (const auto& file : userRplFiles)
            presetFiles.add(file);
    }

    // 1. Check same directory as loaded JSFX file (any .rpl files)
    juce::File jsfxDirectory = jsfxFile.getParentDirectory();

    if (jsfxDirectory.exists())
    {
        auto localRplFiles = jsfxDirectory.findChildFiles(juce::File::findFiles, false, "*.rpl");

        for (const auto& file : localRplFiles)
            presetFiles.add(file);
    }

    // 2. Add presets from persistent storage directories
    auto dirString = apvts.state.getProperty("presetDirectories", "").toString();

    if (dirString.isNotEmpty())
    {
        juce::StringArray directories;
        directories.addLines(dirString);

        for (const auto& dirPath : directories)
        {
            juce::File dir(dirPath);
            if (dir.exists() && dir.isDirectory())
            {
                auto storedPresets = dir.findChildFiles(juce::File::findFiles, false, "*.rpl");

                for (const auto& file : storedPresets)
                    presetFiles.add(file);
            }
        }
    }

    // 3. Check REAPER Effects directory (recursive, filtered by JSFX name)
    auto reaperEffectsPath = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                 .getChildFile("REAPER")
                                 .getChildFile("Effects");

    if (reaperEffectsPath.exists())
    {
        auto rplFiles = reaperEffectsPath.findChildFiles(juce::File::findFiles, true, "*.rpl");

        // Filter to only include files matching current JSFX name
        for (const auto& file : rplFiles)
        {
            juce::String filename = file.getFileNameWithoutExtension();
            // Match if filename equals JSFX name (case-insensitive)
            if (filename.equalsIgnoreCase(jsfxName))
                presetFiles.add(file);
        }
    }

    return presetFiles;
}

void PresetLoader::updatePresetsInState(juce::ValueTree newPresetsTree)
{
    // This runs on the message thread - safe to modify APVTS
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Remove old presets node if it exists
    auto oldPresetsNode = apvts.state.getChildWithName("presets");
    if (oldPresetsNode.isValid())
        apvts.state.removeChild(oldPresetsNode, nullptr);

    // Add new presets node
    if (newPresetsTree.isValid() && newPresetsTree.getNumChildren() > 0)
    {
        apvts.state.appendChild(newPresetsTree, nullptr);
    }
    else
    {
    }

    // Count total banks for logging
    int totalBanks = 0;
    for (int i = 0; i < newPresetsTree.getNumChildren(); ++i)
        totalBanks += newPresetsTree.getChild(i).getNumChildren();
}
