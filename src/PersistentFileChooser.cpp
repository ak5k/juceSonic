#include "PersistentFileChooser.h"

PersistentFileChooser::PersistentFileChooser(
    const juce::String& settingsKey,
    const juce::String& description,
    const juce::String& filePattern,
    const juce::File& defaultDirectory
)
    : m_settingsKey(settingsKey)
    , m_description(description)
    , m_filePattern(filePattern)
    , m_defaultDirectory(defaultDirectory)
{
    initializeGlobalProperties();
}

void PersistentFileChooser::launchAsync(std::function<void(const juce::File&)> callback, int flags)
{
    juce::File startDirectory = getLastDirectory();

    m_fileChooser = std::make_unique<juce::FileChooser>(m_description, startDirectory, m_filePattern);

    m_fileChooser->launchAsync(
        flags,
        [this, callback](const juce::FileChooser& fc)
        {
            auto selectedFile = fc.getResult();

            if (selectedFile != juce::File{})
            {
                // Remember the parent directory for next time
                setLastDirectory(selectedFile.getParentDirectory());
            }

            // Call the user's callback
            if (callback)
                callback(selectedFile);
        }
    );
}

juce::File PersistentFileChooser::getLastDirectory() const
{
    if (auto* userSettings = m_globalProperties.getUserSettings())
    {
        juce::String lastDir = userSettings->getValue(m_settingsKey, m_defaultDirectory.getFullPathName());
        juce::File directory(lastDir);

        // Validate the directory exists and is actually a directory
        if (directory.exists() && directory.isDirectory())
            return directory;
    }

    return m_defaultDirectory;
}

void PersistentFileChooser::setLastDirectory(const juce::File& directory)
{
    if (!directory.isDirectory())
        return;

    if (auto* userSettings = m_globalProperties.getUserSettings())
    {
        userSettings->setValue(m_settingsKey, directory.getFullPathName());
        userSettings->saveIfNeeded();
    }
}

void PersistentFileChooser::initializeGlobalProperties()
{
    juce::PropertiesFile::Options options;
    options.applicationName = JucePlugin_Name;
    options.filenameSuffix = ".properties";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = JucePlugin_Name;
    options.commonToAllUsers = false;

    m_globalProperties.setStorageParameters(options);
}
