#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/**
 * Reusable file chooser that remembers the last used directory per settings key.
 * Eliminates duplicate directory management code across file operations.
 */
class PersistentFileChooser
{
public:
    /**
     * Constructor
     * @param settingsKey Unique key for storing the directory in global settings
     * @param description Dialog description text
     * @param filePattern File pattern filter (e.g., "*.jsfx" or "*")
     * @param defaultDirectory Fallback directory if no previous directory exists
     */
    PersistentFileChooser(
        const juce::String& settingsKey,
        const juce::String& description,
        const juce::String& filePattern = "*",
        const juce::File& defaultDirectory = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
    );

    /**
     * Launch the file chooser asynchronously
     * @param callback Function called with selected file (empty File if cancelled)
     * @param flags FileBrowserComponent flags (default: openMode | canSelectFiles)
     */
    void launchAsync(
        std::function<void(const juce::File&)> callback,
        int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
    );

    /**
     * Get the last used directory for this settings key
     */
    juce::File getLastDirectory() const;

    /**
     * Set the last used directory for this settings key
     */
    void setLastDirectory(const juce::File& directory);

private:
    juce::String m_settingsKey;
    juce::String m_description;
    juce::String m_filePattern;
    juce::File m_defaultDirectory;

    std::unique_ptr<juce::FileChooser> m_fileChooser;

    // Global properties management (shared across instances)
    mutable juce::ApplicationProperties m_globalProperties;
    void initializeGlobalProperties();
};