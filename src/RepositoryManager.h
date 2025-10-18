#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
#include <set>
#include <map>
#include <functional>

class AudioPluginAudioProcessor;

/**
 * @brief Manages JSFX repositories and installations
 *
 * Handles loading repository index.xml files, parsing metadata,
 * and installing JSFX effects to the local data directory.
 */
class RepositoryManager
{
public:
    struct JSFXPackage
    {
        juce::String name;
        juce::String type;
        juce::String description;
        juce::String category;
        juce::String author;
        juce::String version;
        juce::String changelog;
        juce::String mainFileUrl;
        juce::String repositoryName;                                     // Index name from <index name="...">
        std::vector<std::pair<juce::String, juce::String>> dependencies; // <relativePath, url>
    };

    struct Repository
    {
        juce::String url;
        juce::String name;
        juce::String commit;
        bool isValid = false;
        std::vector<JSFXPackage> packages;
    };

    explicit RepositoryManager(AudioPluginAudioProcessor& processor);
    ~RepositoryManager();

    /**
     * @brief Load repository URLs from persistent storage
     */
    void loadRepositories();

    /**
     * @brief Save repository URLs to persistent storage
     */
    void saveRepositories();

    /**
     * @brief Get all configured repository URLs
     */
    juce::StringArray getRepositoryUrls() const;

    /**
     * @brief Set repository URLs (one per line)
     */
    void setRepositoryUrls(const juce::StringArray& urls);

    /**
     * @brief Fetch and parse a repository index.xml
     * @param url URL to index.xml file
     * @param callback Called with parsed repository or error message
     */
    void fetchRepository(const juce::String& url, std::function<void(Repository, juce::String)> callback);

    /**
     * @brief Download and install a JSFX package
     * @param package Package metadata with URLs
     * @param callback Called with success/failure and message
     */
    void installPackage(const JSFXPackage& package, std::function<void(bool, juce::String)> callback);

    /**
     * @brief Uninstall a JSFX package
     * @param package Package metadata
     * @param callback Called with success/failure and message
     */
    void uninstallPackage(const JSFXPackage& package, std::function<void(bool, juce::String)> callback);

    /**
     * @brief Cancel any ongoing installation operations
     */
    void cancelInstallation();

    /**
     * @brief Get the installation directory for a package
     * @param package Package metadata
     * @return Directory path where package should be installed
     */
    juce::File getPackageInstallDirectory(const JSFXPackage& package) const;

    /**
     * @brief Check if a package is already installed
     */
    bool isPackageInstalled(const JSFXPackage& package) const;

    /**
     * @brief Get the installed version of a package
     * @return Version string, or empty if not installed
     */
    juce::String getInstalledVersion(const JSFXPackage& package) const;

    /**
     * @brief Get the base data directory for JSFX installations
     */
    juce::File getDataDirectory() const;

    /**
     * @brief Check if a package is pinned (excluded from batch operations)
     */
    bool isPackagePinned(const JSFXPackage& package) const;

    /**
     * @brief Set pin state for a package
     */
    void setPackagePinned(const JSFXPackage& package, bool pinned);

    /**
     * @brief Check if a package is ignored (hidden from view)
     */
    bool isPackageIgnored(const JSFXPackage& package) const;

    /**
     * @brief Set ignore state for a package
     */
    void setPackageIgnored(const JSFXPackage& package, bool ignored);

    // Cancellation flag (public so UI can reset it)
    std::atomic<bool> shouldCancelInstallation{false};

private:
    /**
     * @brief Parse index.xml content into Repository structure
     */
    Repository parseRepositoryXml(const juce::String& xmlContent, const juce::String& sourceUrl);

    /**
     * @brief Download a file from URL
     */
    bool downloadFile(const juce::String& url, const juce::File& destination, juce::String& errorMessage);

    /**
     * @brief Sanitize a filename/directory name
     */
    juce::String sanitizeFilename(const juce::String& name) const;

    /**
     * @brief Generate unique key for a package (for pin/ignore tracking)
     */
    juce::String getPackageKey(const JSFXPackage& package) const;

    /**
     * @brief Load pin/ignore states from persistent storage
     */
    void loadPackageStates();

    /**
     * @brief Save pin/ignore states to persistent storage
     */
    void savePackageStates();

    AudioPluginAudioProcessor& processor;
    juce::StringArray repositoryUrls;
    std::set<juce::String> pinnedPackages;
    std::set<juce::String> ignoredPackages;
    std::map<juce::String, juce::String> installedVersions; // packageKey -> version

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RepositoryManager)
};
