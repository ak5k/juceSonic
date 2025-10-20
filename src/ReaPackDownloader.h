#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <memory>
#include "ReaPackIndexParser.h"

/**
 * Downloads and caches JSFX files from ReaPack repositories.
 */
class ReaPackDownloader : private juce::Thread
{
public:
    struct DownloadResult
    {
        bool success = false;
        juce::String errorMessage;
        juce::File downloadedFile;
    };

    using DownloadCallback = std::function<void(const DownloadResult&)>;

    ReaPackDownloader();
    ~ReaPackDownloader() override;

    /**
     * Download a ReaPack index from URL (with caching).
     * If forceRefresh is false, will check cache first and only download if remote is newer.
     * @param indexUrl URL to the ReaPack index.xml
     * @param callback Called on message thread when download completes
     * @param forceRefresh If true, always download fresh index (default: false)
     */
    void downloadIndex(
        const juce::URL& indexUrl,
        std::function<void(bool success, std::vector<ReaPackIndexParser::JsfxEntry>)> callback,
        bool forceRefresh = false
    );

    /**
     * Get cached index entries for a repository URL.
     * @param indexUrl URL to the ReaPack index.xml
     * @return Cached entries, or empty vector if not cached
     */
    std::vector<ReaPackIndexParser::JsfxEntry> getCachedIndex(const juce::URL& indexUrl) const;

    /**
     * Download a JSFX package (main file + all associated graphics/data files).
     * @param entry The JSFX entry with all source files
     * @param callback Called on message thread when all downloads complete
     */
    void downloadJsfx(const ReaPackIndexParser::JsfxEntry& entry, DownloadCallback callback);

    /**
     * Get the cache directory where downloaded JSFX files are stored.
     */
    juce::File getCacheDirectory() const;

    /**
     * Check if a JSFX entry is already cached.
     */
    bool isCached(const ReaPackIndexParser::JsfxEntry& entry) const;

    /**
     * Get the cached file for a JSFX entry (if it exists).
     */
    juce::File getCachedFile(const ReaPackIndexParser::JsfxEntry& entry) const;

    /**
     * Clear all cached downloads.
     */
    void clearCache();

    /**
     * Clear cached files for a specific JSFX package.
     * @param entry The JSFX entry to clear from cache
     * @return true if package was cached and deleted, false otherwise
     */
    bool clearPackageCache(const ReaPackIndexParser::JsfxEntry& entry);

private:
    juce::File cacheDir;
    juce::File indexCacheDir;
    ReaPackIndexParser parser;

    void run() override;

    struct DownloadTask
    {
        juce::URL url;
        juce::File targetFile;
        std::function<void(bool, juce::String)> callback;
    };

    juce::CriticalSection taskLock;
    std::queue<DownloadTask> downloadQueue;

    void processDownloadQueue();
    juce::String sanitizeFilename(const juce::String& filename) const;
    bool isPathWithin(const juce::File& base, const juce::File& candidate) const;
    juce::String getIndexCacheFilename(const juce::URL& indexUrl) const;
    juce::File getIndexCacheFile(const juce::URL& indexUrl) const;
    juce::File getIndexTimestampFile(const juce::URL& indexUrl) const;
};
