#include "RepositoryWindow.h"
#include "PluginProcessor.h"
#include "ReaperPresetConverter.h"

RepositoryWindow::RepositoryWindow(AudioPluginAudioProcessor& proc)
    : processor(proc)
    , repositoryManager(std::make_unique<RepositoryManager>(proc))
    , repositoryTreeView(*repositoryManager)
{
    setLookAndFeel(&sharedLookAndFeel->lf);

    // Setup repository tree view with callbacks
    addAndMakeVisible(repositoryTreeView);
    repositoryTreeView.onInstallPackage = [this](const RepositoryManager::JSFXPackage& pkg) { installPackage(pkg); };
    repositoryTreeView.onUninstallPackage = [this](const RepositoryManager::JSFXPackage& pkg)
    { uninstallPackage(pkg); };
    repositoryTreeView.onBatchInstallPackages = [this](const std::vector<RepositoryManager::JSFXPackage>& pkgs)
    { batchInstallPackages(pkgs); };
    repositoryTreeView.onBatchUninstallPackages = [this](const std::vector<RepositoryManager::JSFXPackage>& pkgs)
    { batchUninstallPackages(pkgs); };
    repositoryTreeView.onSelectionChangedCallback = [this]() { updateButtonsForSelection(); };

    // Setup command callback for Enter key
    repositoryTreeView.onCommand = [this](const juce::Array<juce::TreeViewItem*>& selectedItems)
    {
        // Collect all packages from selected items
        juce::Array<RepositoryTreeItem*> repoItems;
        for (auto* item : selectedItems)
            if (auto* repoItem = dynamic_cast<RepositoryTreeItem*>(item))
                repoItems.add(repoItem);

        if (repoItems.isEmpty())
            return;

        // Determine action based on first package's state
        if (auto* firstItem = repoItems[0])
        {
            if (firstItem->getType() == RepositoryTreeItem::ItemType::Package && firstItem->getPackage())
            {
                bool shouldInstall = !repositoryManager->isPackageInstalled(*firstItem->getPackage());

                if (shouldInstall)
                    repositoryTreeView.installFromTreeItems(selectedItems);
                else
                    repositoryTreeView.uninstallFromTreeItems(selectedItems);
            }
        }
    };

    // Setup repository controls
    addAndMakeVisible(manageReposButton);
    manageReposButton.setButtonText("Repositories...");
    manageReposButton.onClick = [this]() { showRepositoryEditor(); };

    addAndMakeVisible(refreshButton);
    refreshButton.setButtonText("Refresh");
    refreshButton.onClick = [this]() { refreshRepositoryList(); };

    addAndMakeVisible(installButton);
    installButton.setButtonText("Install Selected");
    installButton.setEnabled(false);
    installButton.onClick = [this]() { installSelectedPackage(); };

    addAndMakeVisible(installAllButton);
    installAllButton.setButtonText("Install All");
    installAllButton.setEnabled(false);
    installAllButton.onClick = [this]() { installAllPackages(); };

    addAndMakeVisible(cancelButton);
    cancelButton.setButtonText("Cancel");
    cancelButton.setEnabled(false);
    cancelButton.onClick = [this]()
    {
        repositoryManager->cancelInstallation();
        statusLabel.setText("Cancelling...", juce::dontSendNotification);
        cancelButton.setEnabled(false);
    };

    addAndMakeVisible(statusLabel);
    statusLabel.setText("", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);

    setSize(600, 600);
    setWantsKeyboardFocus(true);

    // Start loading repositories
    refreshRepositoryList();
}

RepositoryWindow::~RepositoryWindow()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void RepositoryWindow::visibilityChanged()
{
    // Refresh installation status when window becomes visible
    if (isVisible() && !allPackages.empty())

        repositoryTreeView.getTreeView().repaint();
}

void RepositoryWindow::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void RepositoryWindow::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    // Top controls
    auto topBar = bounds.removeFromTop(30);
    manageReposButton.setBounds(topBar.removeFromLeft(170));
    topBar.removeFromLeft(5);
    refreshButton.setBounds(topBar.removeFromLeft(80));

    bounds.removeFromTop(10);

    // Status at bottom
    auto statusBar = bounds.removeFromBottom(25);
    statusLabel.setBounds(statusBar);
    bounds.removeFromBottom(5);

    // Install buttons
    auto buttonBar = bounds.removeFromBottom(30);
    installAllButton.setBounds(buttonBar.removeFromRight(100));
    buttonBar.removeFromRight(5);
    installButton.setBounds(buttonBar.removeFromRight(150));
    buttonBar.removeFromRight(5);
    cancelButton.setBounds(buttonBar.removeFromRight(80));
    bounds.removeFromBottom(10);

    // Repository tree view (includes built-in search)
    repositoryTreeView.setBounds(bounds);
}

// (ListBox painting replaced by TreeView items)

// ListBox-related methods removed; selection is handled via TreeView

void RepositoryWindow::timerCallback()
{
    // Update status label with animated dots while loading
    if (isLoading)
    {
        static int dots = 0;
        dots = (dots + 1) % 4;
        juce::String loadingText = "Loading repositories";
        for (int i = 0; i < dots; ++i)
            loadingText += ".";
        statusLabel.setText(loadingText, juce::dontSendNotification);
    }
    else
    {
        // Not loading anymore, stop the timer
        stopTimer();
    }
}

void RepositoryWindow::refreshRepositoryList()
{
    // Clear any selection and reset button labels
    installButton.setButtonText("Install Selected");
    installButton.setEnabled(false);

    isLoading = true;
    statusLabel.setText("Loading repositories...", juce::dontSendNotification);
    refreshButton.setEnabled(false);
    startTimer(500);

    repositories.clear();
    allPackages.clear();

    auto urls = repositoryManager->getRepositoryUrls();
    if (urls.isEmpty())
    {
        isLoading = false;
        stopTimer();
        statusLabel.setText(
            "No repositories configured. Click 'Repositories' to add some.",
            juce::dontSendNotification
        );
        refreshButton.setEnabled(true);
        return;
    }

    // Fetch all repositories
    auto remaining = std::make_shared<std::atomic<int>>(urls.size());

    // Reserve space to prevent reallocation invalidating pointers
    repositories.reserve(urls.size());

    for (const auto& url : urls)
    {
        repositoryManager->fetchRepository(
            url,
            [this, remaining](RepositoryManager::Repository repo, juce::String error)
            {
                if (error.isEmpty() && repo.isValid)
                {
                    // Store the repository
                    repositories.push_back(repo);

                    // Store all packages in flat list
                    for (const auto& pkg : repo.packages)
                        allPackages.push_back(pkg);
                }

                int count = --(*remaining);
                if (count == 0)
                {
                    // All repositories loaded - update the tree view
                    repositoryTreeView.setRepositories(repositories);
                    repositoryTreeView.setAllPackages(allPackages);
                    repositoryTreeView.refreshRepositories();

                    isLoading = false;

                    juce::String status = juce::String(allPackages.size())
                                        + " JSFX available from "
                                        + juce::String(repositories.size())
                                        + " repositor"
                                        + (repositories.size() == 1 ? "y" : "ies");
                    statusLabel.setText(status, juce::dontSendNotification);
                    refreshButton.setEnabled(true);
                    installAllButton.setEnabled(!allPackages.empty());

                    if (!allPackages.empty())
                    {
                        installButton.setButtonText("Install Selected");
                        installButton.setEnabled(true);
                    }

                    // Check for version mismatches after loading
                    checkForVersionMismatches();
                }
            }
        );
    }
}

void RepositoryWindow::checkForVersionMismatches()
{
    struct VersionMismatch
    {
        juce::String packageName;
        juce::String installedVersion;
        juce::String availableVersion;
        const RepositoryManager::JSFXPackage* package;
    };

    std::vector<VersionMismatch> mismatches;

    // Check all packages for version mismatches
    for (const auto& pkg : allPackages)
    {
        // Only check installed packages (including pinned ones)
        if (!repositoryManager->isPackageInstalled(pkg))
            continue;

        // Skip ignored packages
        if (repositoryManager->isPackageIgnored(pkg))
            continue;

        juce::String installedVersion = repositoryManager->getInstalledVersion(pkg);
        juce::String availableVersion = pkg.version;

        // Simple string comparison - any difference is a mismatch
        if (installedVersion != availableVersion && installedVersion.isNotEmpty())
        {
            VersionMismatch mismatch;
            mismatch.packageName = pkg.name;
            mismatch.installedVersion = installedVersion;
            mismatch.availableVersion = availableVersion;
            mismatch.package = &pkg;
            mismatches.push_back(mismatch);
        }
    }

    if (mismatches.empty())
        return;

    // Build message
    juce::String message = "New version(s) available:\n\n";
    for (const auto& m : mismatches)
        message += m.packageName + ": " + m.installedVersion + " → " + m.availableVersion + "\n";

    // Show dialog with Update All option
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::AlertWindow::InfoIcon)
            .withTitle("Version Mismatches")
            .withMessage(message)
            .withButton("Update All")
            .withButton("Cancel"),
        [this, mismatches](int result)
        {
            if (result == 1) // Update All button
            {
                // Install all packages with mismatches (except pinned ones)
                int totalToUpdate = 0;
                for (const auto& m : mismatches)
                    if (!repositoryManager->isPackagePinned(*m.package))
                        totalToUpdate++;

                if (totalToUpdate == 0)
                {
                    statusLabel.setText("All mismatched packages are pinned", juce::dontSendNotification);
                    return;
                }

                auto packagesToUpdate = std::make_shared<std::vector<RepositoryManager::JSFXPackage>>();
                for (const auto& m : mismatches)
                    if (!repositoryManager->isPackagePinned(*m.package))
                        packagesToUpdate->push_back(*m.package);

                auto updated = std::make_shared<std::atomic<int>>(0);
                auto failed = std::make_shared<std::atomic<int>>(0);

                installButton.setEnabled(false);
                installAllButton.setEnabled(false);
                refreshButton.setEnabled(false);

                for (const auto& package : *packagesToUpdate)
                {
                    repositoryManager->installPackage(
                        package,
                        [this, updated, failed, totalToUpdate](bool success, juce::String message)
                        {
                            if (success)
                                (*updated)++;
                            else
                                (*failed)++;

                            int completed = (*updated) + (*failed);
                            statusLabel.setText(
                                "Updating... " + juce::String(completed) + "/" + juce::String(totalToUpdate),
                                juce::dontSendNotification
                            );

                            if (completed >= totalToUpdate)
                            {
                                statusLabel.setText(
                                    "Update complete: "
                                        + juce::String(*updated)
                                        + " updated, "
                                        + juce::String(*failed)
                                        + " failed",
                                    juce::dontSendNotification
                                );

                                installButton.setEnabled(true);
                                installAllButton.setEnabled(true);
                                refreshButton.setEnabled(true);
                                repositoryTreeView.getTreeView().repaint();
                                updateButtonsForSelection();
                            }
                        }
                    );
                }
            }
        }
    );
}

void RepositoryWindow::installSelectedPackage()
{
    // Check if button says "Uninstall Selected"
    if (installButton.getButtonText().startsWith("Uninstall"))
    {
        uninstallSelectedPackage();
        return;
    }

    // If no selection, treat as Install All
    auto selected = repositoryTreeView.getSelectedRepoItems();
    if (selected.isEmpty())
    {
        installAllPackages();
        return;
    }

    // Helper lambda to collect packages recursively from a tree item
    std::function<void(RepositoryTreeItem*, std::vector<RepositoryManager::JSFXPackage>&)> collectPackages;
    collectPackages = [&](RepositoryTreeItem* item, std::vector<RepositoryManager::JSFXPackage>& packages)
    {
        if (item->getType() == RepositoryTreeItem::ItemType::Package)
        {
            if (auto* pkg = item->getPackage())
                packages.push_back(*pkg);
        }
        else if (item->getType() != RepositoryTreeItem::ItemType::Metadata)
        {
            // Recurse into children (skip metadata items)
            for (int i = 0; i < item->getNumSubItems(); ++i)
                if (auto* sub = dynamic_cast<RepositoryTreeItem*>(item->getSubItem(i)))
                    collectPackages(sub, packages);
        }
    };

    // Build list of packages to install from selection
    std::vector<RepositoryManager::JSFXPackage> toInstall;
    for (auto* item : selected)
        collectPackages(item, toInstall);

    if (toInstall.empty())
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "Install",
            "No packages found in selection.",
            this,
            nullptr
        );
        return;
    }

    // Install packages sequentially (reuse existing proceedWithInstallation pattern)
    auto packagesToInstall = std::make_shared<std::vector<RepositoryManager::JSFXPackage>>(toInstall);
    auto totalToInstall = packagesToInstall->size();
    auto installed = std::make_shared<std::atomic<int>>(0);
    auto failed = std::make_shared<std::atomic<int>>(0);

    installButton.setEnabled(false);
    installAllButton.setEnabled(false);
    refreshButton.setEnabled(false);

    for (size_t i = 0; i < packagesToInstall->size(); ++i)
    {
        const auto& package = (*packagesToInstall)[i];
        repositoryManager->installPackage(
            package,
            [this, package, installed, failed, totalToInstall, i](bool success, juce::String message)
            {
                if (success)
                    (*installed)++;
                else
                    (*failed)++;
                int completed = (*installed) + (*failed);

                statusLabel.setText(
                    "Installing... " + juce::String(completed) + "/" + juce::String(totalToInstall),
                    juce::dontSendNotification
                );

                if (completed >= static_cast<int>(totalToInstall))
                {
                    statusLabel.setText(
                        "Installation complete: "
                            + juce::String(*installed)
                            + " installed, "
                            + juce::String(*failed)
                            + " failed",
                        juce::dontSendNotification
                    );

                    installButton.setEnabled(true);
                    installAllButton.setEnabled(true);
                    refreshButton.setEnabled(true);
                    repositoryTreeView.getTreeView().repaint();
                    updateButtonsForSelection();
                }
            }
        );
    }
}

void RepositoryWindow::installAllPackages()
{
    if (allPackages.empty())

        return;

    // Check if button says "Uninstall All"
    if (installAllButton.getButtonText() == "Uninstall All")
    {
        uninstallAllPackages();
        return;
    }

    int toInstall = static_cast<int>(allPackages.size());

    // Confirm installation - using async version to avoid blocking
    // Get the top-level window to ensure dialog appears in front
    auto* topLevel = getTopLevelComponent();

    // Use async message box with proper modal callback
    juce::MessageBoxOptions options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::QuestionIcon)
        .withTitle("Install All Packages")
        .withMessage("Install/update all " + juce::String(toInstall) + " package" + (toInstall == 1 ? "" : "s") + "?\n\n"
                    "This will install new packages and update existing ones.")
        .withButton("OK")
        .withButton("Cancel")
        .withAssociatedComponent(topLevel);

    juce::NativeMessageBox::showAsync(
        options,
        [this, toInstall](int result)
        {
            if (result != 0) // Not the OK button (first button = 0)

                return;

            // Proceed with installation
            proceedWithInstallation();
        }
    );
}

void RepositoryWindow::proceedWithInstallation()
{
    // Install ALL packages (including already installed ones to update/reinstall)
    auto packagesToInstall = std::make_shared<std::vector<RepositoryManager::JSFXPackage>>();
    for (const auto& package : allPackages)
        packagesToInstall->push_back(package);

    int toInstall = static_cast<int>(packagesToInstall->size());

    // Disable buttons during installation
    installButton.setEnabled(false);
    installAllButton.setEnabled(false);
    refreshButton.setEnabled(false);

    statusLabel.setText("Preparing to install " + juce::String(toInstall) + " packages...", juce::dontSendNotification);

    auto totalToInstall = packagesToInstall->size();
    auto installed = std::make_shared<std::atomic<int>>(0);
    auto failed = std::make_shared<std::atomic<int>>(0);

    if (totalToInstall == 0)
    {
        installButton.setEnabled(true);
        installAllButton.setEnabled(true);
        refreshButton.setEnabled(true);
        return;
    }

    for (size_t i = 0; i < packagesToInstall->size(); ++i)
    {
        const auto& package = (*packagesToInstall)[i];

        repositoryManager->installPackage(
            package,
            [this, package, installed, failed, totalToInstall, i](bool success, juce::String message)
            {
                if (success)
                    (*installed)++;
                else
                    (*failed)++;

                int completed = (*installed) + (*failed);

                DBG("Progress: "
                    << completed
                    << "/"
                    << totalToInstall
                    << " (installed: "
                    << *installed
                    << ", failed: "
                    << *failed
                    << ")");

                statusLabel.setText(
                    "Installing... " + juce::String(completed) + "/" + juce::String(totalToInstall),
                    juce::dontSendNotification
                );

                if (completed >= static_cast<int>(totalToInstall))
                {
                    // All done
                    juce::String resultMessage =
                        "Installed: " + juce::String(*installed) + "\n" + "Failed: " + juce::String(*failed);

                    statusLabel.setText(
                        "Installation complete: "
                            + juce::String(*installed)
                            + " installed, "
                            + juce::String(*failed)
                            + " failed",
                        juce::dontSendNotification
                    );

                    installButton.setEnabled(true);
                    installAllButton.setEnabled(true);
                    refreshButton.setEnabled(true);
                    // Update tree and buttons
                    repositoryTreeView.getTreeView().repaint(); // Update INSTALLED badges
                    updateButtonsForSelection();                // Update button text

                    juce::NativeMessageBox::showMessageBoxAsync(
                        *failed > 0 ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                        "Installation Complete",
                        resultMessage,
                        this,
                        nullptr
                    );
                }
            }
        );
    }
}

void RepositoryWindow::uninstallSelectedPackage()
{
    auto selected = repositoryTreeView.getSelectedRepoItems();
    if (selected.isEmpty())
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "Uninstall",
            "Please select packages to uninstall.",
            this,
            nullptr
        );
        return;
    }

    juce::String message = "Uninstall selected packages?\n\n";
    for (auto* it : selected)
        if (it->getType() == RepositoryTreeItem::ItemType::Package)
            message += it->getName() + "\n";

    juce::MessageBoxOptions options = juce::MessageBoxOptions()
                                          .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle("Uninstall Packages")
                                          .withMessage(message)
                                          .withButton("OK")
                                          .withButton("Cancel")
                                          .withAssociatedComponent(getTopLevelComponent());

    juce::NativeMessageBox::showAsync(
        options,
        [this, selected](int result)
        {
            if (result != 0)
                return;

            // Helper to collect packages recursively
            std::function<void(RepositoryTreeItem*, std::vector<const RepositoryManager::JSFXPackage*>&)>
                collectPackages;
            collectPackages =
                [&](RepositoryTreeItem* item, std::vector<const RepositoryManager::JSFXPackage*>& packages)
            {
                if (item->getType() == RepositoryTreeItem::ItemType::Package)
                {
                    if (auto* pkg = item->getPackage())
                        packages.push_back(pkg);
                }
                else if (item->getType() != RepositoryTreeItem::ItemType::Metadata)
                {
                    // Recurse (skip metadata items)
                    for (int i = 0; i < item->getNumSubItems(); ++i)
                        if (auto* sub = dynamic_cast<RepositoryTreeItem*>(item->getSubItem(i)))
                            collectPackages(sub, packages);
                }
            };

            std::vector<const RepositoryManager::JSFXPackage*> toUninstall;
            for (auto* it : selected)
                collectPackages(it, toUninstall);

            int uninstalled = 0, failed = 0;
            for (auto* pkg : toUninstall)
            {
                auto installDir = repositoryManager->getPackageInstallDirectory(*pkg);
                if (installDir.exists() && installDir.deleteRecursively())
                    uninstalled++;
                else
                    failed++;
            }

            repositoryTreeView.getTreeView().repaint();
            updateButtonsForSelection();

            juce::NativeMessageBox::showMessageBoxAsync(
                failed > 0 ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                "Uninstallation Complete",
                "Uninstalled " + juce::String(uninstalled) + " packages.\nFailed: " + juce::String(failed),
                this,
                nullptr
            );
        }
    );
}

void RepositoryWindow::uninstallAllPackages()
{
    if (allPackages.empty())
        return;

    int installedCount = 0;
    for (const auto& package : allPackages)
        if (repositoryManager->isPackageInstalled(package))
            installedCount++;

    if (installedCount == 0)
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No Packages Installed",
            "There are no installed packages to uninstall.",
            this,
            nullptr
        );
        return;
    }

    // Confirm uninstallation
    auto* topLevel = getTopLevelComponent();
    juce::MessageBoxOptions options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Uninstall All Packages")
        .withMessage("Uninstall all " + juce::String(installedCount) + " installed package" + (installedCount == 1 ? "" : "s") + "?\n\n"
                    "This will delete all installed JSFX files from this repository.")
        .withButton("OK")
        .withButton("Cancel")
        .withAssociatedComponent(topLevel);

    juce::NativeMessageBox::showAsync(
        options,
        [this, installedCount](int result)
        {
            if (result != 0) // Not the OK button (first button = 0)
                return;

            int uninstalled = 0;
            int failed = 0;

            for (const auto& package : allPackages)
            {
                if (repositoryManager->isPackageInstalled(package))
                {
                    auto installDir = repositoryManager->getPackageInstallDirectory(package);
                    if (installDir.exists() && installDir.deleteRecursively())
                        uninstalled++;
                    else
                        failed++;
                }
            }

            repositoryTreeView.getTreeView().repaint();
            updateButtonsForSelection();

            juce::String message =
                "Uninstalled " + juce::String(uninstalled) + " package" + (uninstalled == 1 ? "" : "s");
            if (failed > 0)
                message += "\nFailed to uninstall " + juce::String(failed) + " package" + (failed == 1 ? "" : "s");

            statusLabel.setText(message, juce::dontSendNotification);

            juce::NativeMessageBox::showMessageBoxAsync(
                failed > 0 ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                "Uninstallation Complete",
                message,
                this,
                nullptr
            );
        }
    );
}

void RepositoryWindow::updateInstallAllButtonText()
{
    if (allPackages.empty())
    {
        installAllButton.setButtonText("Install All");
        return;
    }

    int installedCount = 0;
    for (const auto& package : allPackages)
        if (repositoryManager->isPackageInstalled(package))
            installedCount++;

    if (installedCount == static_cast<int>(allPackages.size()))
        installAllButton.setButtonText("Uninstall All");
    else
        installAllButton.setButtonText("Install All");
}

void RepositoryWindow::updateButtonsForSelection()
{
    auto selected = repositoryTreeView.getSelectedRepoItems();

    if (selected.isEmpty())
    {
        // No selection - default to Install/Uninstall All based on overall state
        installButton.setButtonText("Install Selected");
        installButton.setEnabled(false);
        updateInstallAllButtonText();
        return;
    }

    // Check if any selected item (or its children) represents an installed package
    bool hasInstalledPackage = false;

    std::function<void(RepositoryTreeItem*)> checkInstalled;
    checkInstalled = [&](RepositoryTreeItem* item)
    {
        if (item->getType() == RepositoryTreeItem::ItemType::Category
            || item->getType() == RepositoryTreeItem::ItemType::Package)
        {
            if (auto* pkg = item->getPackage())
            {
                if (repositoryManager->isPackageInstalled(*pkg))
                {
                    hasInstalledPackage = true;
                    return;
                }
            }
        }

        // Check children for packages
        if (!hasInstalledPackage)
        {
            for (int i = 0; i < item->getNumSubItems(); ++i)
                if (auto* sub = dynamic_cast<RepositoryTreeItem*>(item->getSubItem(i)))
                    checkInstalled(sub);
        }
    };

    for (auto* item : selected)
        if (!hasInstalledPackage)
            checkInstalled(item);

    // Update buttons based on whether selection contains installed packages
    if (hasInstalledPackage)
    {
        installButton.setButtonText("Uninstall Selected");
        installAllButton.setButtonText("Uninstall All");
    }
    else
    {
        installButton.setButtonText("Install Selected");
        installAllButton.setButtonText("Install All");
    }

    installButton.setEnabled(true);
}

// Helper Methods

juce::String RepositoryWindow::PackageCollectionResult::getSkipMessage() const
{
    if (skippedPinned == 0 && skippedIgnored == 0)
        return {};

    juce::String message = " (skipped ";
    if (skippedPinned > 0)
        message += juce::String(skippedPinned) + " pinned";
    if (skippedPinned > 0 && skippedIgnored > 0)
        message += ", ";
    if (skippedIgnored > 0)
        message += juce::String(skippedIgnored) + " ignored";
    message += ")";
    return message;
}

RepositoryWindow::PackageCollectionResult
RepositoryWindow::collectPackagesFromTreeItem(RepositoryTreeItem* item, bool installedOnly)
{
    PackageCollectionResult result;
    if (!item)
        return result;

    std::function<void(RepositoryTreeItem*)> collectPackages;
    collectPackages = [&](RepositoryTreeItem* treeItem)
    {
        if (treeItem->getType() == RepositoryTreeItem::ItemType::Package)
        {
            if (auto* pkg = treeItem->getPackage())
            {
                bool isInstalled = repositoryManager->isPackageInstalled(*pkg);

                // Skip based on operation type
                if (installedOnly ? !isInstalled : isInstalled)
                    return;

                // Skip pinned packages
                if (repositoryManager->isPackagePinned(*pkg))
                {
                    result.skippedPinned++;
                    return;
                }

                // Skip ignored packages
                if (repositoryManager->isPackageIgnored(*pkg))
                {
                    result.skippedIgnored++;
                    return;
                }

                result.packages.push_back(*pkg);
            }
        }
        else
        {
            for (int i = 0; i < treeItem->getNumSubItems(); ++i)
                if (auto* child = dynamic_cast<RepositoryTreeItem*>(treeItem->getSubItem(i)))
                    collectPackages(child);
        }
    };

    collectPackages(item);
    return result;
}

void RepositoryWindow::setButtonsEnabled(bool enabled)
{
    installButton.setEnabled(enabled);
    installAllButton.setEnabled(enabled);
    refreshButton.setEnabled(enabled);
    cancelButton.setEnabled(!enabled); // Cancel is enabled when other buttons are disabled
}

void RepositoryWindow::showConfirmationDialog(
    const juce::String& title,
    const juce::String& message,
    std::function<void()> onConfirm
)
{
    juce::MessageBoxOptions options = juce::MessageBoxOptions()
                                          .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle(title)
                                          .withMessage(message)
                                          .withButton("OK")
                                          .withButton("Cancel")
                                          .withAssociatedComponent(getTopLevelComponent());

    juce::NativeMessageBox::showAsync(
        options,
        [onConfirm](int result)
        {
            if (result == 0) // OK button
                onConfirm();
        }
    );
}

void RepositoryWindow::executeBatchOperation(
    const std::vector<RepositoryManager::JSFXPackage>& packages,
    const juce::String& skipMessage,
    bool isInstall
)
{
    if (packages.empty())
        return;

    // Reset cancellation flag before starting
    repositoryManager->shouldCancelInstallation.store(false);

    setButtonsEnabled(false);

    const juce::String operationCaps = isInstall ? "Install" : "Uninstall";
    const juce::String operationPastTense = isInstall ? "installed" : "uninstalled";

    // Create shared state for sequential processing
    auto packagesPtr = std::make_shared<std::vector<RepositoryManager::JSFXPackage>>(packages);
    auto currentIndex = std::make_shared<size_t>(0);
    auto succeeded = std::make_shared<int>(0);
    auto failed = std::make_shared<int>(0);

    // Create recursive lambda for sequential processing
    auto processNext = std::make_shared<std::function<void()>>();
    *processNext = [this,
                    packagesPtr,
                    currentIndex,
                    succeeded,
                    failed,
                    skipMessage,
                    operationCaps,
                    operationPastTense,
                    isInstall,
                    processNext]()
    {
        if (*currentIndex >= packagesPtr->size())
        {
            // All done
            statusLabel.setText(
                operationCaps
                    + " complete: "
                    + juce::String(*succeeded)
                    + " "
                    + operationPastTense
                    + ", "
                    + juce::String(*failed)
                    + " failed"
                    + skipMessage,
                juce::dontSendNotification
            );

            setButtonsEnabled(true);
            repositoryTreeView.getTreeView().repaint();
            updateButtonsForSelection();
            return;
        }

        const auto& package = (*packagesPtr)[*currentIndex];
        size_t packageNum = *currentIndex + 1;

        statusLabel.setText(
            operationCaps
                + "ing "
                + package.name
                + " ("
                + juce::String(packageNum)
                + "/"
                + juce::String(packagesPtr->size())
                + ")...",
            juce::dontSendNotification
        );

        auto callback = [this, currentIndex, succeeded, failed, processNext](bool success, juce::String message)
        {
            if (success)
                (*succeeded)++;
            else
                (*failed)++;

            (*currentIndex)++;
            (*processNext)(); // Process next package
        };

        if (isInstall)
            repositoryManager->installPackage(package, callback);
        else
            repositoryManager->uninstallPackage(package, callback);
    };

    // Start processing first package
    (*processNext)();
}

// Package Operations

void RepositoryWindow::installPackage(const RepositoryManager::JSFXPackage& package)
{
    // Single package install is just a batch of 1
    showConfirmationDialog(
        "Install Package",
        "Install " + package.name + "?",
        [this, package]()
        {
            std::vector<RepositoryManager::JSFXPackage> singlePackage = {package};
            executeBatchOperation(singlePackage, "", true);
        }
    );
}

void RepositoryWindow::uninstallPackage(const RepositoryManager::JSFXPackage& package)
{
    // Single package uninstall is just a batch of 1
    showConfirmationDialog(
        "Uninstall Package",
        "Uninstall " + package.name + "?",
        [this, package]()
        {
            std::vector<RepositoryManager::JSFXPackage> singlePackage = {package};
            executeBatchOperation(singlePackage, "", false);
        }
    );
}

void RepositoryWindow::batchInstallPackages(const std::vector<RepositoryManager::JSFXPackage>& packages)
{
    if (packages.empty())
        return;

    // Single confirmation for all packages
    juce::String message = packages.size() == 1 ? "Install " + packages[0].name + "?"
                                                : "Install " + juce::String(packages.size()) + " packages?";

    showConfirmationDialog(
        "Install Packages",
        message,
        [this, packages]() { executeBatchOperation(packages, "", true); }
    );
}

void RepositoryWindow::batchUninstallPackages(const std::vector<RepositoryManager::JSFXPackage>& packages)
{
    if (packages.empty())
        return;

    // Single confirmation for all packages
    juce::String message = packages.size() == 1 ? "Uninstall " + packages[0].name + "?"
                                                : "Uninstall " + juce::String(packages.size()) + " packages?";

    showConfirmationDialog(
        "Uninstall Packages",
        message,
        [this, packages]() { executeBatchOperation(packages, "", false); }
    );
}

void RepositoryWindow::showRepositoryEditor()
{
    auto* editor = new RepositoryEditorDialog(
        *repositoryManager,
        [this]()
        {
            // Refresh after editing
            refreshRepositoryList();
        }
    );

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(editor);
    options.dialogTitle = "Repositories";
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

//==============================================================================
// RepositoryEditorDialog
//==============================================================================

RepositoryEditorDialog::RepositoryEditorDialog(RepositoryManager& repoManager, std::function<void()> onClose)
    : repositoryManager(repoManager)
    , closeCallback(onClose)
{
    setLookAndFeel(&sharedLookAndFeel->lf);

    addAndMakeVisible(instructionsLabel);
    instructionsLabel.setMultiLine(true);
    instructionsLabel.setReadOnly(true);
    instructionsLabel.setScrollbarsShown(false);
    instructionsLabel.setCaretVisible(false);
    instructionsLabel.setPopupMenuEnabled(true);
    instructionsLabel.setText(
        "Enter ReaPack compatible JSFX repository URLs, one per line:\n"
        "Example: https://raw.githubusercontent.com/JoepVanlier/JSFX/master/index.xml"
    );
    instructionsLabel.setFont(juce::FontOptions(12.0f));
    instructionsLabel.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    instructionsLabel.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);

    addAndMakeVisible(repositoryEditor);
    repositoryEditor.setMultiLine(true);
    repositoryEditor.setReturnKeyStartsNewLine(true);
    repositoryEditor.setScrollbarsShown(true);
    repositoryEditor.setFont(juce::FontOptions(12.0f));

    // Load current repositories
    auto urls = repositoryManager.getRepositoryUrls();
    repositoryEditor.setText(urls.joinIntoString("\n"));

    addAndMakeVisible(saveButton);
    saveButton.setButtonText("Save");
    saveButton.onClick = [this]() { saveAndClose(); };

    addAndMakeVisible(cancelButton);
    cancelButton.setButtonText("Cancel");
    cancelButton.onClick = [this]() { cancel(); };

    setSize(600, 400);
}

RepositoryEditorDialog::~RepositoryEditorDialog()
{
    setLookAndFeel(nullptr);
}

void RepositoryEditorDialog::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void RepositoryEditorDialog::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    instructionsLabel.setBounds(bounds.removeFromTop(40));
    bounds.removeFromTop(5);

    auto buttonBar = bounds.removeFromBottom(30);
    cancelButton.setBounds(buttonBar.removeFromRight(80));
    buttonBar.removeFromRight(5);
    saveButton.setBounds(buttonBar.removeFromRight(80));
    bounds.removeFromBottom(10);

    repositoryEditor.setBounds(bounds);
}

void RepositoryEditorDialog::saveAndClose()
{
    // Parse URLs from text editor
    juce::StringArray urls;
    auto lines = juce::StringArray::fromLines(repositoryEditor.getText());

    for (const auto& line : lines)
    {
        auto trimmed = line.trim();
        if (trimmed.isNotEmpty() && !trimmed.startsWith("#"))
            urls.add(trimmed);
    }

    repositoryManager.setRepositoryUrls(urls);

    if (closeCallback)
        closeCallback();

    // Close dialog
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(1);
}

void RepositoryEditorDialog::cancel()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(0);
}
