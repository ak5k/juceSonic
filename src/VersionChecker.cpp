#include "VersionChecker.h"

VersionChecker::VersionChecker()
    : juce::Thread("VersionChecker")
{
}

VersionChecker::~VersionChecker()
{
    cancelCheck();
}

void VersionChecker::checkForUpdates(const juce::String& currentVer, const juce::String& repo)
{
    if (isThreadRunning())
        return; // Already checking

    currentVersion = currentVer;
    repoUrl = repo;
    startThread();
}

void VersionChecker::cancelCheck()
{
    signalThreadShouldExit();
    if (downloadTask)
        downloadTask.reset();
    stopThread(2000);
}

juce::String VersionChecker::normalizeVersion(const juce::String& version)
{
    // Remove 'v' prefix if present (e.g., "v1.0.0" -> "1.0.0")
    auto normalized = version.trim();
    if (normalized.startsWithChar('v') || normalized.startsWithChar('V'))
        normalized = normalized.substring(1);
    return normalized;
}

bool VersionChecker::compareVersions(const juce::String& current, const juce::String& latest)
{
    // Normalize both versions
    auto currentNorm = normalizeVersion(current);
    auto latestNorm = normalizeVersion(latest);

    // Split into components
    auto currentParts = juce::StringArray::fromTokens(currentNorm, ".", "");
    auto latestParts = juce::StringArray::fromTokens(latestNorm, ".", "");

    // Compare major.minor.patch
    for (int i = 0; i < juce::jmax(currentParts.size(), latestParts.size()); ++i)
    {
        int currentVal = i < currentParts.size() ? currentParts[i].getIntValue() : 0;
        int latestVal = i < latestParts.size() ? latestParts[i].getIntValue() : 0;

        if (latestVal > currentVal)
            return true; // Update available
        else if (latestVal < currentVal)
            return false; // Current is newer
    }

    return false; // Versions are equal
}

void VersionChecker::run()
{
    if (repoUrl.isEmpty())
    {
        if (onUpdateCheckComplete)
            juce::MessageManager::callAsync([this]() { onUpdateCheckComplete(false, {}, {}); });
        return;
    }

    // Extract owner/repo from URL
    // Expected formats: "https://github.com/owner/repo" or "owner/repo"
    auto urlParts = juce::StringArray::fromTokens(repoUrl, "/", "");
    juce::String owner, repo;

    if (urlParts.size() >= 2)
    {
        owner = urlParts[urlParts.size() - 2];
        repo = urlParts[urlParts.size() - 1];
    }

    if (owner.isEmpty() || repo.isEmpty())
    {
        if (onUpdateCheckComplete)
            juce::MessageManager::callAsync([this]() { onUpdateCheckComplete(false, {}, {}); });
        return;
    }

    // Build GitHub API URL for latest release
    juce::String apiUrl = "https://api.github.com/repos/" + owner + "/" + repo + "/releases/latest";

    // Fetch latest release info
    juce::URL url(apiUrl);
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(5000)
            .withNumRedirectsToFollow(5)
    );

    if (threadShouldExit())
        return;

    if (!stream)
    {
        if (onUpdateCheckComplete)
            juce::MessageManager::callAsync([this]() { onUpdateCheckComplete(false, {}, {}); });
        return;
    }

    // Read response
    auto response = stream->readEntireStreamAsString();

    if (threadShouldExit())
        return;

    // Parse JSON response
    auto json = juce::JSON::parse(response);
    if (auto* obj = json.getDynamicObject())
    {
        auto tagName = obj->getProperty("tag_name").toString();
        auto htmlUrl = obj->getProperty("html_url").toString();

        if (tagName.isNotEmpty())
        {
            bool updateAvailable = compareVersions(currentVersion, tagName);

            if (onUpdateCheckComplete)
            {
                auto latestVer = tagName;
                auto downloadUrl = htmlUrl;
                juce::MessageManager::callAsync([this, updateAvailable, latestVer, downloadUrl]()
                                                { onUpdateCheckComplete(updateAvailable, latestVer, downloadUrl); });
            }
            return;
        }
    }

    // Failed to parse or get version
    if (onUpdateCheckComplete)
        juce::MessageManager::callAsync([this]() { onUpdateCheckComplete(false, {}, {}); });
}
