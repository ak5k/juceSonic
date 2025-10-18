#pragma once

#include <juce_core/juce_core.h>

/**
 * @brief Centralized file I/O operations protected by a global interprocess lock
 *
 * All file read/write operations in juceSonic should go through this class to ensure
 * thread-safety and process-safety across multiple plugin instances.
 */
class FileIO
{
public:
    // File reading operations
    static juce::String readFile(const juce::File& file);
    static std::unique_ptr<juce::XmlElement> readXml(const juce::File& file);

    // File writing operations
    static bool writeFile(const juce::File& file, const juce::String& content);
    static bool writeXml(const juce::File& file, const juce::XmlElement& xml);

    // File operations
    static bool copyFile(const juce::File& source, const juce::File& destination);
    static bool deleteFile(const juce::File& file);

    // Directory operations
    static juce::Result createDirectory(const juce::File& directory);
    static bool deleteDirectory(const juce::File& directory);

    // Check operations (read-only, still locked for consistency)
    static bool exists(const juce::File& file);
    static bool isDirectory(const juce::File& file);

    // RAII lock wrapper - public for direct use when needed
    class ScopedFileLock
    {
    public:
        ScopedFileLock()
            : lock(getGlobalFileLock())
        {
            lock.enter();
        }

        ~ScopedFileLock()
        {
            lock.exit();
        }

        ScopedFileLock(const ScopedFileLock&) = delete;
        ScopedFileLock& operator=(const ScopedFileLock&) = delete;

    private:
        juce::InterProcessLock& lock;
    };

private:
    // Global interprocess lock for all file operations
    static juce::InterProcessLock& getGlobalFileLock();
};
