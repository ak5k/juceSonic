#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

/**
 * @brief Checks for new versions of juceSonic on GitHub
 *
 * Compares current version with latest release tag from GitHub repo.
 * Supports version formats: "X.Y.Z" and "vX.Y.Z"
 */
class VersionChecker : private juce::Thread
{
public:
    VersionChecker();
    ~VersionChecker() override;

    /**
     * @brief Start checking for updates asynchronously
     *
     * @param currentVersion Current version string (e.g., "0.1.0")
     * @param repoUrl GitHub repository URL (e.g., "https://github.com/ak5k/jucesonic")
     */
    void checkForUpdates(const juce::String& currentVersion, const juce::String& repoUrl);

    /**
     * @brief Callback when update check completes
     *
     * Parameters: bool updateAvailable, String latestVersion, String downloadUrl
     */
    std::function<void(bool, const juce::String&, const juce::String&)> onUpdateCheckComplete;

    /**
     * @brief Cancel any ongoing check
     */
    void cancelCheck();

private:
    void run() override;
    juce::String normalizeVersion(const juce::String& version);
    bool compareVersions(const juce::String& current, const juce::String& latest);

    juce::String currentVersion;
    juce::String repoUrl;
    std::unique_ptr<juce::URL::DownloadTask> downloadTask;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VersionChecker)
};
