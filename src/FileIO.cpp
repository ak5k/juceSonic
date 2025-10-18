#include "FileIO.h"

juce::InterProcessLock& FileIO::getGlobalFileLock()
{
    // Static instance ensures single lock across all plugin instances
    // Name is system-wide and unique to juceSonic to prevent conflicts with other apps
    static juce::InterProcessLock globalLock("juceSonic_GlobalFileLock");
    return globalLock;
}

juce::String FileIO::readFile(const juce::File& file)
{
    ScopedFileLock lock;
    return file.loadFileAsString();
}

std::unique_ptr<juce::XmlElement> FileIO::readXml(const juce::File& file)
{
    ScopedFileLock lock;
    return juce::parseXML(file);
}

bool FileIO::writeFile(const juce::File& file, const juce::String& content)
{
    ScopedFileLock lock;

    // Ensure parent directory exists
    auto parentDir = file.getParentDirectory();
    if (!parentDir.exists())
    {
        auto result = parentDir.createDirectory();
        if (!result.wasOk())
            return false;
    }

    return file.replaceWithText(content);
}

bool FileIO::writeXml(const juce::File& file, const juce::XmlElement& xml)
{
    // Convert XML to string first (no file I/O, just serialization)
    auto xmlString = xml.toString();

    // Use writeFile which handles its own locking
    // This avoids holding the lock while writeTo() creates a TemporaryFile
    return writeFile(file, xmlString);
}

bool FileIO::copyFile(const juce::File& source, const juce::File& destination)
{
    ScopedFileLock lock;
    return source.copyFileTo(destination);
}

bool FileIO::deleteFile(const juce::File& file)
{
    ScopedFileLock lock;
    return file.deleteFile();
}

juce::Result FileIO::createDirectory(const juce::File& directory)
{
    ScopedFileLock lock;
    return directory.createDirectory();
}

bool FileIO::deleteDirectory(const juce::File& directory)
{
    ScopedFileLock lock;
    return directory.deleteRecursively();
}

bool FileIO::exists(const juce::File& file)
{
    ScopedFileLock lock;
    return file.exists();
}

bool FileIO::isDirectory(const juce::File& file)
{
    ScopedFileLock lock;
    return file.isDirectory();
}
