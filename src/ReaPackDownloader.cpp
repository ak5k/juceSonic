#include "ReaPackDownloader.h"
#include "FileIO.h"

ReaPackDownloader::ReaPackDownloader()
    : Thread("ReaPackDownloader")
{
    // Create cache directory in user's app data
    cacheDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("juceSonic")
                   .getChildFile("ReaPackCache");

    indexCacheDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("juceSonic")
                        .getChildFile("ReaPackIndexCache");

    FileIO::createDirectory(cacheDir);
    FileIO::createDirectory(indexCacheDir);
    startThread();
}

ReaPackDownloader::~ReaPackDownloader()
{
    stopThread(5000);
}

void ReaPackDownloader::downloadIndex(
    const juce::URL& indexUrl,
    std::function<void(bool, std::vector<ReaPackIndexParser::JsfxEntry>)> callback,
    bool forceRefresh
)
{
    // If not forcing refresh, try to use cached index
    if (!forceRefresh)
    {
        juce::File cachedIndexFile = getIndexCacheFile(indexUrl);

        if (FileIO::exists(cachedIndexFile))
        {
            // Load from cache
            juce::String xmlContent = FileIO::readFile(cachedIndexFile);
            auto entries = parser.parseIndex(xmlContent);

            // Return cached entries immediately
            juce::MessageManager::callAsync([callback, entries]() { callback(true, entries); });
            return;
        }
    }

    // Download index in background thread
    juce::Thread::launch(
        [this, indexUrl, callback]()
        {
            bool success = false;
            std::vector<ReaPackIndexParser::JsfxEntry> entries;

            auto inputStream = indexUrl.createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(10000)
            );

            if (inputStream != nullptr)
            {
                juce::String xmlContent = inputStream->readEntireStreamAsString();
                entries = parser.parseIndex(xmlContent);
                success = !entries.empty();

                if (success)
                {
                    // Cache the index
                    juce::File cachedIndexFile = getIndexCacheFile(indexUrl);
                    FileIO::writeFile(cachedIndexFile, xmlContent);

                    // Store timestamp of the newest entry for future comparison
                    juce::String newestTimestamp;
                    for (const auto& entry : entries)
                    {
                        if (entry.timestamp.isEmpty())
                            continue;

                        if (newestTimestamp.isEmpty() || entry.timestamp > newestTimestamp)
                            newestTimestamp = entry.timestamp;
                    }

                    if (newestTimestamp.isNotEmpty())
                    {
                        juce::File timestampFile = getIndexTimestampFile(indexUrl);
                        FileIO::writeFile(timestampFile, newestTimestamp);
                    }
                }
            }

            // Call callback on message thread
            juce::MessageManager::callAsync([callback, success, entries]() { callback(success, entries); });
        }
    );
}

void ReaPackDownloader::downloadJsfx(const ReaPackIndexParser::JsfxEntry& entry, DownloadCallback callback)
{
    // Always check cache first - never auto-update
    if (isCached(entry))
    {
        DownloadResult result;
        result.success = true;
        result.downloadedFile = getCachedFile(entry);

        juce::MessageManager::callAsync([callback, result]() { callback(result); });
        return;
    }

    // Download all source files for this package
    if (entry.sources.empty())
    {
        DownloadResult result;
        result.success = false;
        result.errorMessage = "No source files to download";

        juce::MessageManager::callAsync([callback, result]() { callback(result); });
        return;
    }

    // Create package directory (sanitized package name)
    juce::String packageName = entry.name.upToLastOccurrenceOf(".", false, false);
    juce::File packageDir = cacheDir.getChildFile(sanitizeFilename(packageName));
    FileIO::createDirectory(packageDir);

    struct PendingSource
    {
        juce::URL url;
        juce::File targetFile;
    };

    std::vector<PendingSource> pendingSources;
    pendingSources.reserve(entry.sources.size());

    for (const auto& source : entry.sources)
    {
        juce::File targetFile;

        if (source.file.isNotEmpty())
        {
            targetFile = packageDir.getChildFile(source.file);

            if (!isPathWithin(packageDir, targetFile))
            {
                DownloadResult result;
                result.success = false;
                result.errorMessage = "Blocked download with invalid relative path: " + source.file;
                juce::MessageManager::callAsync([callback, result]() { callback(result); });
                return;
            }
        }
        else
        {
            juce::URL url(source.url);
            targetFile = packageDir.getChildFile(sanitizeFilename(url.getFileName()));
        }

        pendingSources.push_back({juce::URL(source.url), targetFile});
    }

    if (pendingSources.empty())
    {
        DownloadResult result;
        result.success = false;
        result.errorMessage = "No valid source files to download";
        juce::MessageManager::callAsync([callback, result]() { callback(result); });
        return;
    }

    // Main JSFX file is the first source (ReaPack convention)
    juce::File mainFile = pendingSources.front().targetFile;

    auto sourceCount = std::make_shared<std::atomic<int>>(static_cast<int>(pendingSources.size()));
    auto failedCount = std::make_shared<std::atomic<int>>(0);
    auto errorMessages = std::make_shared<juce::StringArray>();

    for (const auto& pending : pendingSources)
    {
        FileIO::createDirectory(pending.targetFile.getParentDirectory());

        DownloadTask task;
        task.url = pending.url;
        task.targetFile = pending.targetFile;
        task.callback =
            [callback, sourceCount, failedCount, errorMessages, mainFile](bool success, juce::String errorMsg)
        {
            if (!success)
            {
                (*failedCount)++;
                errorMessages->add(errorMsg);
            }

            if (--(*sourceCount) == 0)
            {
                DownloadResult result;
                result.success = (*failedCount) == 0;
                result.errorMessage = errorMessages->joinIntoString("\n");
                result.downloadedFile = mainFile;

                juce::MessageManager::callAsync([callback, result]() { callback(result); });
            }
        };

        juce::ScopedLock lock(taskLock);
        downloadQueue.push(task);
    }

    notify();
}

juce::File ReaPackDownloader::getCacheDirectory() const
{
    return cacheDir;
}

bool ReaPackDownloader::isCached(const ReaPackIndexParser::JsfxEntry& entry) const
{
    // Check if package directory exists
    juce::String packageName = entry.name.upToLastOccurrenceOf(".", false, false);
    juce::File packageDir = cacheDir.getChildFile(sanitizeFilename(packageName));

    if (!FileIO::exists(packageDir))
        return false;

    // Simple check: if directory exists and all source files are present, it's cached
    // Version tracking is handled by JsfxPluginTreeView in reapack.xml
    // Cached packages are not auto-updated - user must explicitly update them

    // Check if all source files exist
    for (const auto& source : entry.sources)
    {
        juce::File sourceFile;
        if (source.file.isNotEmpty())
            sourceFile = packageDir.getChildFile(source.file);
        else
            sourceFile = packageDir.getChildFile(sanitizeFilename(juce::URL(source.url).getFileName()));

        if (!FileIO::exists(sourceFile))
            return false;
    }

    return true;
}

juce::File ReaPackDownloader::getCachedFile(const ReaPackIndexParser::JsfxEntry& entry) const
{
    // Return path to main JSFX file in package directory
    juce::String packageName = entry.name.upToLastOccurrenceOf(".", false, false);
    juce::File packageDir = cacheDir.getChildFile(sanitizeFilename(packageName));

    // Use the first source file path (the main JSFX file)
    if (!entry.sources.empty() && entry.sources[0].file.isNotEmpty())
        return packageDir.getChildFile(entry.sources[0].file);

    // Fallback: extract just the filename from entry.name (last part after /)
    juce::String filename = entry.name.fromLastOccurrenceOf("/", false, false);
    return packageDir.getChildFile(filename);
}

void ReaPackDownloader::clearCache()
{
    FileIO::deleteDirectory(cacheDir);
    FileIO::createDirectory(cacheDir);
}

bool ReaPackDownloader::clearPackageCache(const ReaPackIndexParser::JsfxEntry& entry)
{
    // Get the package directory
    juce::String packageName = entry.name.upToLastOccurrenceOf(".", false, false);
    juce::File packageDir = cacheDir.getChildFile(sanitizeFilename(packageName));

    // Delete the package directory recursively (includes all source files)
    if (packageDir.exists())
    {
        bool success = packageDir.deleteRecursively();
        return success;
    }

    return false;
}

void ReaPackDownloader::run()
{
    while (!threadShouldExit())
    {
        processDownloadQueue();
        wait(500);
    }
}

void ReaPackDownloader::processDownloadQueue()
{
    DownloadTask task;

    {
        juce::ScopedLock lock(taskLock);
        if (downloadQueue.empty())
            return;

        task = downloadQueue.front();
        downloadQueue.pop();
    }

    // Download file
    bool success = false;
    juce::String errorMessage;

    auto inputStream = task.url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(30000)
    );

    if (inputStream != nullptr)
    {
        // Delete existing file to prevent appending
        if (task.targetFile.existsAsFile())
            task.targetFile.deleteFile();

        juce::FileOutputStream outputStream(task.targetFile);
        if (outputStream.openedOk())
        {
            outputStream.writeFromInputStream(*inputStream, -1);
            success = outputStream.getStatus().wasOk();

            if (!success)
                errorMessage = "Failed to write file: " + outputStream.getStatus().getErrorMessage();
        }
        else
        {
            errorMessage = "Failed to create output file";
        }
    }
    else
    {
        errorMessage = "Failed to download from URL: " + task.url.toString(false);
    }

    // Call callback
    task.callback(success, errorMessage);
}

juce::String ReaPackDownloader::sanitizeFilename(const juce::String& filename) const
{
    // Remove path separators and invalid characters
    return filename.replaceCharacters("/\\:*?\"<>|", "_________");
}

bool ReaPackDownloader::isPathWithin(const juce::File& base, const juce::File& candidate) const
{
    auto basePath = base.getFullPathName();
    auto candidatePath = candidate.getFullPathName();

    auto separator = juce::File::getSeparatorString();
    if (!basePath.endsWith(separator))
        basePath += separator;

    return candidatePath == base.getFullPathName() || candidatePath.startsWith(basePath);
}

std::vector<ReaPackIndexParser::JsfxEntry> ReaPackDownloader::getCachedIndex(const juce::URL& indexUrl) const
{
    juce::File cachedIndexFile = getIndexCacheFile(indexUrl);

    if (!FileIO::exists(cachedIndexFile))
        return {};

    juce::String xmlContent = FileIO::readFile(cachedIndexFile);
    return parser.parseIndex(xmlContent);
}

juce::String ReaPackDownloader::getIndexCacheFilename(const juce::URL& indexUrl) const
{
    // Create a unique filename from the URL (hash of the URL)
    juce::String urlString = indexUrl.toString(false);
    return juce::String::toHexString(urlString.hashCode64()) + ".xml";
}

juce::File ReaPackDownloader::getIndexCacheFile(const juce::URL& indexUrl) const
{
    return indexCacheDir.getChildFile(getIndexCacheFilename(indexUrl));
}

juce::File ReaPackDownloader::getIndexTimestampFile(const juce::URL& indexUrl) const
{
    juce::String baseFilename = getIndexCacheFilename(indexUrl).upToLastOccurrenceOf(".", false, false);
    return indexCacheDir.getChildFile(baseFilename + ".timestamp");
}
