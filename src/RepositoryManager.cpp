#include "RepositoryManager.h"
#include "PluginProcessor.h"
#include "PresetManager.h"

RepositoryManager::RepositoryManager(AudioPluginAudioProcessor& proc)
    : processor(proc)
{
    loadRepositories();
    loadPackageStates();
}

RepositoryManager::~RepositoryManager()
{
    saveRepositories();
    savePackageStates();
}

void RepositoryManager::loadRepositories()
{
    // Load from ApplicationProperties stored in PresetRootDirectory
    auto propsFile = getDataDirectory().getParentDirectory().getChildFile("repository_list.xml");

    if (propsFile.existsAsFile())
    {
        auto xml = juce::parseXML(propsFile);
        if (xml && xml->hasTagName("RepositoryList"))
        {
            repositoryUrls.clear();
            for (auto* repo : xml->getChildWithTagNameIterator("Repository"))
            {
                auto url = repo->getStringAttribute("url");
                if (url.isNotEmpty())
                    repositoryUrls.add(url);
            }
        }
    }
}

void RepositoryManager::saveRepositories()
{
    auto propsFile = getDataDirectory().getParentDirectory().getChildFile("repository_list.xml");

    juce::XmlElement root("RepositoryList");
    for (const auto& url : repositoryUrls)
    {
        auto* repoElement = root.createNewChildElement("Repository");
        repoElement->setAttribute("url", url);
    }

    propsFile.getParentDirectory().createDirectory();
    root.writeTo(propsFile);
}

juce::StringArray RepositoryManager::getRepositoryUrls() const
{
    return repositoryUrls;
}

void RepositoryManager::setRepositoryUrls(const juce::StringArray& urls)
{
    repositoryUrls = urls;
    saveRepositories();
}

void RepositoryManager::fetchRepository(const juce::String& url, std::function<void(Repository, juce::String)> callback)
{
    // Download XML in background thread
    juce::Thread::launch(
        [this, url, callback]()
        {
            juce::URL repoUrl(url);
            juce::StringPairArray responseHeaders;
            int statusCode = 0;

            auto stream = repoUrl.createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(10000)
                    .withResponseHeaders(&responseHeaders)
                    .withStatusCode(&statusCode)
            );

            if (stream == nullptr || statusCode != 200)
            {
                juce::String error = "Failed to fetch repository: HTTP " + juce::String(statusCode);
                juce::MessageManager::callAsync([callback, error]() { callback(Repository{}, error); });
                return;
            }

            auto xmlContent = stream->readEntireStreamAsString();
            auto repository = parseRepositoryXml(xmlContent, url);

            juce::MessageManager::callAsync([callback, repository]() { callback(repository, juce::String()); });
        }
    );
}

RepositoryManager::Repository
RepositoryManager::parseRepositoryXml(const juce::String& xmlContent, const juce::String& sourceUrl)
{
    Repository repo;
    repo.url = sourceUrl;

    auto xml = juce::parseXML(xmlContent);
    if (!xml || !xml->hasTagName("index"))
        return repo;

    repo.name = xml->getStringAttribute("name", "Unknown Repository");
    repo.commit = xml->getStringAttribute("commit");
    repo.isValid = true;

    // Parse categories and packages
    for (auto* category : xml->getChildWithTagNameIterator("category"))
    {
        juce::String categoryName = category->getStringAttribute("name");

        for (auto* reapack : category->getChildWithTagNameIterator("reapack"))
        {
            juce::String packageName = reapack->getStringAttribute("name");

            // Only support packages ending with .jsfx
            if (!packageName.endsWithIgnoreCase(".jsfx"))
            {
                DBG("Skipping non-JSFX package: " << packageName);
                continue;
            }

            juce::String packageType = reapack->getStringAttribute("type");
            juce::String description = reapack->getStringAttribute("desc");

            // Get latest version
            juce::XmlElement* latestVersion = nullptr;
            for (auto* version : reapack->getChildWithTagNameIterator("version"))
                latestVersion = version; // Last version in XML is typically the latest

            if (latestVersion)
            {
                JSFXPackage package;
                package.name = packageName;
                package.type = packageType;
                package.description = description;
                package.category = categoryName;
                package.author = latestVersion->getStringAttribute("author");
                package.version = latestVersion->getStringAttribute("name");
                package.repositoryName = repo.name; // Use repository's index name

                // Get changelog
                if (auto* changelog = latestVersion->getFirstChildElement())
                {
                    if (changelog->hasTagName("changelog"))
                        package.changelog = changelog->getAllSubText();
                }

                // Parse source files
                for (auto* source : latestVersion->getChildWithTagNameIterator("source"))
                {
                    juce::String fileAttr = source->getStringAttribute("file");
                    juce::String sourceFileUrl = source->getAllSubText().trim();

                    if (fileAttr.isEmpty())
                    {
                        // Main file
                        package.mainFileUrl = sourceFileUrl;
                    }
                    else
                    {
                        // Dependency file
                        package.dependencies.push_back({fileAttr, sourceFileUrl});
                    }
                }

                repo.packages.push_back(package);
            }
        }
    }

    return repo;
}

void RepositoryManager::installPackage(const JSFXPackage& package, std::function<void(bool, juce::String)> callback)
{
    DBG("Installing package: " << package.name);
    DBG("  Repository: " << package.repositoryName);
    DBG("  Author: " << package.author);
    DBG("  Version: " << package.version);

    // Install in background thread
    juce::Thread::launch(
        [this, package, callback]()
        {
            auto installDir = getPackageInstallDirectory(package);

            DBG("  Install directory: " << installDir.getFullPathName());

            // Create installation directory
            auto result = installDir.createDirectory();
            if (!result.wasOk())
            {
                juce::String error = "Failed to create directory: " + result.getErrorMessage();
                juce::MessageManager::callAsync([callback, error]() { callback(false, error); });
                return;
            }

            juce::String errorMsg;

            // Download main file
            auto mainFile = installDir.getChildFile(package.name);
            if (!downloadFile(package.mainFileUrl, mainFile, errorMsg))
            {
                juce::MessageManager::callAsync([callback, errorMsg]() { callback(false, errorMsg); });
                return;
            }

            // Download dependencies
            for (const auto& [relativePath, url] : package.dependencies)
            {
                auto depFile = installDir.getChildFile(relativePath);
                depFile.getParentDirectory().createDirectory();

                if (!downloadFile(url, depFile, errorMsg))
                {
                    juce::MessageManager::callAsync([callback, errorMsg]() { callback(false, errorMsg); });
                    return;
                }
            }

            // Store version information in package states
            auto packageKey = getPackageKey(package);
            installedVersions[packageKey] = package.version;
            savePackageStates();

            // Success
            juce::String successMsg = "Successfully installed " + package.name + " v" + package.version;
            juce::MessageManager::callAsync([callback, successMsg]() { callback(true, successMsg); });
        }
    );
}

void RepositoryManager::uninstallPackage(const JSFXPackage& package, std::function<void(bool, juce::String)> callback)
{
    DBG("Uninstalling package: " << package.name);
    DBG("  Repository: " << package.repositoryName);

    // Uninstall in background thread
    juce::Thread::launch(
        [this, package, callback]()
        {
            auto installDir = getPackageInstallDirectory(package);

            DBG("  Install directory: " << installDir.getFullPathName());

            if (!installDir.exists())
            {
                juce::String error = "Package not found: " + package.name;
                juce::MessageManager::callAsync([callback, error]() { callback(false, error); });
                return;
            }

            // Delete the entire installation directory
            if (!installDir.deleteRecursively())
            {
                juce::String error = "Failed to delete package directory: " + installDir.getFullPathName();
                juce::MessageManager::callAsync([callback, error]() { callback(false, error); });
                return;
            }

            // Remove version information from package states
            auto packageKey = getPackageKey(package);
            installedVersions.erase(packageKey);
            savePackageStates();

            // Success
            juce::String successMsg = "Successfully uninstalled " + package.name;
            juce::MessageManager::callAsync([callback, successMsg]() { callback(true, successMsg); });
        }
    );
}

bool RepositoryManager::downloadFile(const juce::String& url, const juce::File& destination, juce::String& errorMessage)
{
    juce::URL fileUrl(url);
    juce::StringPairArray responseHeaders;
    int statusCode = 0;

    auto stream = fileUrl.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(30000)
            .withResponseHeaders(&responseHeaders)
            .withStatusCode(&statusCode)
    );

    if (stream == nullptr || statusCode != 200)
    {
        errorMessage = "Failed to download " + url + ": HTTP " + juce::String(statusCode);
        return false;
    }

    juce::FileOutputStream output(destination);
    if (!output.openedOk())
    {
        errorMessage = "Failed to open file for writing: " + destination.getFullPathName();
        return false;
    }

    output.writeFromInputStream(*stream, -1);
    output.flush();

    if (output.getStatus().failed())
    {
        errorMessage = "Failed to write file: " + output.getStatus().getErrorMessage();
        return false;
    }

    return true;
}

juce::File RepositoryManager::getPackageInstallDirectory(const JSFXPackage& package) const
{
    // Install to: <AppData>/juceSonic/data/remote/<repository-index-name>/<package-name>/
    auto dataDir = getDataDirectory();

    juce::String repoName = package.repositoryName.isNotEmpty() ? package.repositoryName : "Unknown";
    juce::String packageBaseName = package.name;

    // Remove .jsfx extension if present
    if (packageBaseName.endsWithIgnoreCase(".jsfx"))
        packageBaseName = packageBaseName.dropLastCharacters(5);

    return dataDir.getChildFile("remote")
        .getChildFile(sanitizeFilename(repoName))
        .getChildFile(sanitizeFilename(packageBaseName));
}

bool RepositoryManager::isPackageInstalled(const JSFXPackage& package) const
{
    auto installDir = getPackageInstallDirectory(package);
    auto mainFile = installDir.getChildFile(package.name);
    return mainFile.existsAsFile();
}

juce::String RepositoryManager::getInstalledVersion(const JSFXPackage& package) const
{
    auto packageKey = getPackageKey(package);
    auto it = installedVersions.find(packageKey);

    if (it != installedVersions.end())
        return it->second;

    return juce::String();
}

juce::File RepositoryManager::getDataDirectory() const
{
    // Use same base directory as PresetManager
    auto appData = juce::File::getSpecialLocation(juce::File::SpecialLocationType::userApplicationDataDirectory);

    return appData.getChildFile("juceSonic").getChildFile("data");
}

juce::String RepositoryManager::sanitizeFilename(const juce::String& name) const
{
    // Same sanitization as PresetManager
    juce::String sanitized = name;
    sanitized = sanitized.replaceCharacters("\\/:*?\"<>|", "_________");
    sanitized = sanitized.trim();

    if (sanitized.isEmpty())
        sanitized = "Unknown";

    return sanitized;
}

juce::String RepositoryManager::getPackageKey(const JSFXPackage& package) const
{
    // Create unique key from repository name and package name
    return package.repositoryName + "::" + package.name;
}

bool RepositoryManager::isPackagePinned(const JSFXPackage& package) const
{
    return pinnedPackages.find(getPackageKey(package)) != pinnedPackages.end();
}

void RepositoryManager::setPackagePinned(const JSFXPackage& package, bool pinned)
{
    auto key = getPackageKey(package);
    if (pinned)
        pinnedPackages.insert(key);
    else
        pinnedPackages.erase(key);

    savePackageStates();
}

bool RepositoryManager::isPackageIgnored(const JSFXPackage& package) const
{
    return ignoredPackages.find(getPackageKey(package)) != ignoredPackages.end();
}

void RepositoryManager::setPackageIgnored(const JSFXPackage& package, bool ignored)
{
    auto key = getPackageKey(package);
    if (ignored)
        ignoredPackages.insert(key);
    else
        ignoredPackages.erase(key);

    savePackageStates();
}

void RepositoryManager::loadPackageStates()
{
    auto statesFile = getDataDirectory().getParentDirectory().getChildFile("package_states.xml");

    if (statesFile.existsAsFile())
    {
        auto xml = juce::parseXML(statesFile);
        if (xml && xml->hasTagName("PackageStates"))
        {
            pinnedPackages.clear();
            ignoredPackages.clear();
            installedVersions.clear();

            if (auto* pinnedNode = xml->getChildByName("Pinned"))
            {
                for (auto* pkg : pinnedNode->getChildWithTagNameIterator("Package"))
                {
                    auto key = pkg->getStringAttribute("key");
                    if (key.isNotEmpty())
                        pinnedPackages.insert(key);
                }
            }

            if (auto* ignoredNode = xml->getChildByName("Ignored"))
            {
                for (auto* pkg : ignoredNode->getChildWithTagNameIterator("Package"))
                {
                    auto key = pkg->getStringAttribute("key");
                    if (key.isNotEmpty())
                        ignoredPackages.insert(key);
                }
            }

            if (auto* versionsNode = xml->getChildByName("InstalledVersions"))
            {
                for (auto* pkg : versionsNode->getChildWithTagNameIterator("Package"))
                {
                    auto key = pkg->getStringAttribute("key");
                    auto version = pkg->getStringAttribute("version");
                    if (key.isNotEmpty() && version.isNotEmpty())
                        installedVersions[key] = version;
                }
            }
        }
    }
}

void RepositoryManager::savePackageStates()
{
    auto statesFile = getDataDirectory().getParentDirectory().getChildFile("package_states.xml");

    juce::XmlElement root("PackageStates");

    // Save pinned packages
    auto* pinnedNode = root.createNewChildElement("Pinned");
    for (const auto& key : pinnedPackages)
    {
        auto* pkgElement = pinnedNode->createNewChildElement("Package");
        pkgElement->setAttribute("key", key);
    }

    // Save ignored packages
    auto* ignoredNode = root.createNewChildElement("Ignored");
    for (const auto& key : ignoredPackages)
    {
        auto* pkgElement = ignoredNode->createNewChildElement("Package");
        pkgElement->setAttribute("key", key);
    }

    // Save installed versions
    auto* versionsNode = root.createNewChildElement("InstalledVersions");
    for (const auto& [key, version] : installedVersions)
    {
        auto* pkgElement = versionsNode->createNewChildElement("Package");
        pkgElement->setAttribute("key", key);
        pkgElement->setAttribute("version", version);
    }

    statesFile.getParentDirectory().createDirectory();
    root.writeTo(statesFile);
}
