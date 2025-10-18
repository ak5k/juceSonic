#include "RepositoryWindow.h"
#include "ReaperPresetConverter.h"

// Forward declare RepositoryTreeItem
class RepositoryTreeItem : public juce::TreeViewItem
{
public:
    enum class ItemType
    {
        Index,
        Category,
        Package,
        Metadata
    };

    RepositoryTreeItem(
        const juce::String& name,
        ItemType t,
        const RepositoryManager::JSFXPackage* pkg = nullptr,
        RepositoryWindow* window = nullptr
    )
        : itemName(name)
        , type(t)
        , package(pkg)
        , repositoryWindow(window)
    {
    }

    bool mightContainSubItems() override
    {
        return type == ItemType::Index || type == ItemType::Category;
    }

    bool canBeSelected() const override
    {
        // Index, Category, and Package items can be selected
        return type == ItemType::Index || type == ItemType::Category || type == ItemType::Package;
    }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        if (isSelected())
            g.fillAll(juce::Colours::blue.withAlpha(0.35f));

        g.setColour(type == ItemType::Metadata ? juce::Colours::grey : juce::Colours::white);
        g.setFont(juce::Font(type == ItemType::Metadata ? 11.0f : 14.0f));

        // Main item name
        g.drawText(itemName, 4, 0, width - 100, height, juce::Justification::centredLeft, true);

        if (!repositoryManager)
            return;

        // Show installation status for packages, categories, and indices
        if (type == ItemType::Package && package)
        {
            int badgeX = width - 100;
            const int badgeWidth = 90;

            // Individual package - show installed/not installed
            if (repositoryManager->isPackageInstalled(*package))
            {
                g.setColour(juce::Colours::green);
                g.setFont(juce::Font(11.0f, juce::Font::bold));
                g.drawText("[INSTALLED]", badgeX, 0, badgeWidth, height, juce::Justification::centredRight, true);
                badgeX -= 75;
            }

            // Show pin/ignore indicators
            if (repositoryManager->isPackagePinned(*package))
            {
                g.setColour(juce::Colours::yellow);
                g.setFont(juce::Font(11.0f, juce::Font::bold));
                g.drawText("[PIN]", badgeX, 0, 50, height, juce::Justification::centredRight, true);
                badgeX -= 55;
            }

            if (repositoryManager->isPackageIgnored(*package))
            {
                g.setColour(juce::Colours::grey);
                g.setFont(juce::Font(11.0f, juce::Font::bold));
                g.drawText("[IGNORE]", badgeX, 0, 70, height, juce::Justification::centredRight, true);
            }
        }
        else if (type == ItemType::Category)
        {
            // Category - check if all, some, or none packages are installed
            // Exclude ignored packages from counts
            int totalPackages = 0;
            int installedPackages = 0;

            for (int i = 0; i < getNumSubItems(); ++i)
            {
                if (auto* child = dynamic_cast<RepositoryTreeItem*>(getSubItem(i)))
                {
                    if (child->getType() == ItemType::Package && child->getPackage())
                    {
                        // Skip ignored packages - they don't count
                        if (repositoryManager->isPackageIgnored(*child->getPackage()))
                            continue;

                        totalPackages++;
                        if (repositoryManager->isPackageInstalled(*child->getPackage()))
                            installedPackages++;
                    }
                }
            }

            if (totalPackages > 0)
            {
                if (installedPackages == totalPackages)
                {
                    // All installed
                    g.setColour(juce::Colours::green);
                    g.setFont(juce::Font(11.0f, juce::Font::bold));
                    g.drawText("[INSTALLED]", width - 100, 0, 90, height, juce::Justification::centredRight, true);
                }
                else if (installedPackages > 0)
                {
                    // Partially installed
                    g.setColour(juce::Colours::orange);
                    g.setFont(juce::Font(11.0f));
                    g.drawText(
                        "[" + juce::String(installedPackages) + "/" + juce::String(totalPackages) + "]",
                        width - 100,
                        0,
                        90,
                        height,
                        juce::Justification::centredRight,
                        true
                    );
                }
            }
        }
        else if (type == ItemType::Index)
        {
            // Index/Repository - check if all, some, or none categories are fully installed
            // Exclude ignored packages from counts
            int totalCategories = 0;
            int fullyInstalledCategories = 0;
            int partiallyInstalledCategories = 0;

            for (int i = 0; i < getNumSubItems(); ++i)
            {
                if (auto* categoryItem = dynamic_cast<RepositoryTreeItem*>(getSubItem(i)))
                {
                    if (categoryItem->getType() == ItemType::Category)
                    {
                        totalCategories++;
                        int totalPackages = 0;
                        int installedPackages = 0;

                        for (int j = 0; j < categoryItem->getNumSubItems(); ++j)
                        {
                            if (auto* packageItem = dynamic_cast<RepositoryTreeItem*>(categoryItem->getSubItem(j)))
                            {
                                if (packageItem->getType() == ItemType::Package && packageItem->getPackage())
                                {
                                    // Skip ignored packages - they don't count
                                    if (repositoryManager->isPackageIgnored(*packageItem->getPackage()))
                                        continue;

                                    totalPackages++;
                                    if (repositoryManager->isPackageInstalled(*packageItem->getPackage()))
                                        installedPackages++;
                                }
                            }
                        }

                        if (totalPackages > 0)
                        {
                            if (installedPackages == totalPackages)
                                fullyInstalledCategories++;
                            else if (installedPackages > 0)
                                partiallyInstalledCategories++;
                        }
                    }
                }
            }

            if (totalCategories > 0)
            {
                if (fullyInstalledCategories == totalCategories)
                {
                    // All categories fully installed
                    g.setColour(juce::Colours::green);
                    g.setFont(juce::Font(11.0f, juce::Font::bold));
                    g.drawText("[INSTALLED]", width - 100, 0, 90, height, juce::Justification::centredRight, true);
                }
                else if (fullyInstalledCategories > 0 || partiallyInstalledCategories > 0)
                {
                    // Some categories installed or partially installed
                    g.setColour(juce::Colours::orange);
                    g.setFont(juce::Font(11.0f));
                    juce::String statusText = "[" + juce::String(fullyInstalledCategories);
                    if (partiallyInstalledCategories > 0)
                        statusText += "+" + juce::String(partiallyInstalledCategories);
                    statusText += "/" + juce::String(totalCategories) + "]";
                    g.drawText(statusText, width - 100, 0, 90, height, juce::Justification::centredRight, true);
                }
            }
        }
    }

    void itemSelectionChanged(bool isNowSelected) override
    {
        TreeViewItem::itemSelectionChanged(isNowSelected);
        if (repositoryWindow)
            repositoryWindow->updateButtonsForSelection();
    }

    void itemClicked(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            showContextMenu();
    }

    void showContextMenu()
    {
        juce::PopupMenu menu;

        // Only show menu for Package, Category, or Index items
        if (type == ItemType::Metadata)
            return;

        bool isInstalled = false;
        bool isPinned = false;
        bool isIgnored = false;

        if (type == ItemType::Package && package && repositoryManager)
        {
            isInstalled = repositoryManager->isPackageInstalled(*package);
            isPinned = repositoryManager->isPackagePinned(*package);
            isIgnored = repositoryManager->isPackageIgnored(*package);
        }

        // Add menu items based on type and state
        if (type == ItemType::Package)
        {
            if (isInstalled)
                menu.addItem(1, "Uninstall");
            else
                menu.addItem(2, "Install");

            menu.addSeparator();
            menu.addItem(3, isIgnored ? "Unignore" : "Ignore", true, isIgnored);
            menu.addItem(4, isPinned ? "Unpin" : "Pin", true, isPinned);
        }
        else if (type == ItemType::Category || type == ItemType::Index)
        {
            menu.addItem(5, "Install All");
            menu.addItem(6, "Uninstall All");
            menu.addSeparator();
            menu.addItem(7, "Pin All");
            menu.addItem(8, "Unpin All");
            menu.addItem(9, "Ignore All");
            menu.addItem(10, "Unignore All");
        }

        menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) { handleContextMenuResult(result); });
    }

    void handleContextMenuResult(int result)
    {
        if (result == 0 || !repositoryWindow)
            return;

        switch (result)
        {
        case 1: // Uninstall package
            if (type == ItemType::Package && package)
                repositoryWindow->uninstallPackage(*package);
            break;

        case 2: // Install package
            if (type == ItemType::Package && package)
                repositoryWindow->installPackage(*package);
            break;

        case 3: // Toggle Ignore
            if (type == ItemType::Package && package)
                repositoryWindow->togglePackageIgnored(*package);
            break;

        case 4: // Toggle Pin
            if (type == ItemType::Package && package)
                repositoryWindow->togglePackagePinned(*package);
            break;

        case 5: // Install All
            if (repositoryWindow)
                repositoryWindow->installFromTreeItem(this);
            break;

        case 6: // Uninstall All
            if (repositoryWindow)
                repositoryWindow->uninstallFromTreeItem(this);
            break;

        case 7: // Pin All
            if (repositoryWindow)
                repositoryWindow->pinAllFromTreeItem(this);
            break;

        case 8: // Unpin All
            if (repositoryWindow)
                repositoryWindow->unpinAllFromTreeItem(this);
            break;

        case 9: // Ignore All
            if (repositoryWindow)
                repositoryWindow->ignoreAllFromTreeItem(this);
            break;

        case 10: // Unignore All
            if (repositoryWindow)
                repositoryWindow->unignoreAllFromTreeItem(this);
            break;
        }
    }

    juce::String getName() const
    {
        return itemName;
    }

    const RepositoryManager::JSFXPackage* getPackage() const
    {
        return package;
    }

    ItemType getType() const
    {
        return type;
    }

    void setRepositoryManager(RepositoryManager* mgr)
    {
        repositoryManager = mgr;
    }

    void setRepositoryWindow(RepositoryWindow* window)
    {
        repositoryWindow = window;
    }

private:
    juce::String itemName;
    ItemType type;
    const RepositoryManager::JSFXPackage* package;
    RepositoryManager* repositoryManager = nullptr;
    RepositoryWindow* repositoryWindow = nullptr;
};

RepositoryWindow::RepositoryWindow(RepositoryManager& repoManager)
    : repositoryManager(repoManager)
{
    // Setup repository controls
    addAndMakeVisible(manageReposButton);
    manageReposButton.setButtonText("Manage Repositories...");
    manageReposButton.onClick = [this]() { showRepositoryEditor(); };

    addAndMakeVisible(refreshButton);
    refreshButton.setButtonText("Refresh");
    refreshButton.onClick = [this]() { refreshRepositoryList(); };

    // Setup tree view
    addAndMakeVisible(repoTree);
    repoTree.setMultiSelectEnabled(true);
    repoTree.setRootItemVisible(false); // Hide the root "Repositories" item

    addAndMakeVisible(installButton);
    installButton.setButtonText("Install Selected");
    installButton.setEnabled(false);
    installButton.onClick = [this]() { installSelectedPackage(); };

    addAndMakeVisible(installAllButton);
    installAllButton.setButtonText("Install All");
    installAllButton.setEnabled(false);
    installAllButton.onClick = [this]() { installAllPackages(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setText("", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);

    setSize(600, 600); // Narrower width to fit controls

    // Start loading repositories
    refreshRepositoryList();
}

RepositoryWindow::~RepositoryWindow()
{
    stopTimer();
    repoTree.setRootItem(nullptr);
}

void RepositoryWindow::visibilityChanged()
{
    // Refresh installation status when window becomes visible
    if (isVisible() && !allPackages.empty())
    {
        DBG("RepositoryWindow became visible - refreshing installation status");
        repoTree.repaint();
    }
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

    // Install button
    auto buttonBar = bounds.removeFromBottom(30);
    installAllButton.setBounds(buttonBar.removeFromRight(100));
    buttonBar.removeFromRight(5);
    installButton.setBounds(buttonBar.removeFromRight(150));
    bounds.removeFromBottom(10);

    // Tree
    repoTree.setBounds(bounds);
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
    installButton.setEnabled(false); // Will be enabled after packages load

    isLoading = true;
    statusLabel.setText("Loading repositories...", juce::dontSendNotification);
    refreshButton.setEnabled(false);
    startTimer(500);

    // Clear previous tree
    repoTree.setRootItem(nullptr);
    rootItem.reset();

    repositories.clear();
    allPackages.clear();

    auto urls = repositoryManager.getRepositoryUrls();
    if (urls.isEmpty())
    {
        isLoading = false;
        stopTimer();
        statusLabel.setText(
            "No repositories configured. Click 'Manage Repositories' to add some.",
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
        repositoryManager.fetchRepository(
            url,
            [this, remaining](RepositoryManager::Repository repo, juce::String error)
            {
                if (error.isEmpty() && repo.isValid)
                {
                    // Store the repository first
                    repositories.push_back(repo);
                    auto& storedRepo = repositories.back();

                    // Build tree: index-name -> categories -> packages
                    if (!rootItem)
                    {
                        rootItem = std::make_unique<RepositoryTreeItem>(
                            "Repositories",
                            RepositoryTreeItem::ItemType::Index,
                            nullptr,
                            this
                        );
                        rootItem->setRepositoryManager(&repositoryManager);
                    }

                    auto* indexItem =
                        new RepositoryTreeItem(storedRepo.name, RepositoryTreeItem::ItemType::Index, nullptr, this);
                    indexItem->setRepositoryManager(&repositoryManager);
                    rootItem->addSubItem(indexItem);

                    for (size_t pkgIdx = 0; pkgIdx < storedRepo.packages.size(); ++pkgIdx)
                    {
                        const auto& pkg = storedRepo.packages[pkgIdx];
                        // Get stable pointer from the stored repository
                        const auto* pkgPtr = &storedRepo.packages[pkgIdx];

                        // Store in flat list for some operations
                        allPackages.push_back(pkg);

                        // find or create category
                        RepositoryTreeItem* catItem = nullptr;
                        for (int i = 0; i < indexItem->getNumSubItems(); ++i)
                        {
                            if (auto* c = dynamic_cast<RepositoryTreeItem*>(indexItem->getSubItem(i)))
                            {
                                if (c->getName() == pkg.category)
                                {
                                    catItem = c;
                                    break;
                                }
                            }
                        }

                        if (!catItem)
                        {
                            catItem = new RepositoryTreeItem(
                                pkg.category,
                                RepositoryTreeItem::ItemType::Category,
                                nullptr, // Categories don't need a package pointer
                                this
                            );
                            catItem->setRepositoryManager(&repositoryManager);
                            indexItem->addSubItem(catItem);
                        }
                        // If category already exists, just use it - each package will add its own entry

                        // Add package name as metadata item
                        auto* pkgNameItem =
                            new RepositoryTreeItem(pkg.name, RepositoryTreeItem::ItemType::Package, pkgPtr, this);
                        pkgNameItem->setRepositoryManager(&repositoryManager);
                        catItem->addSubItem(pkgNameItem);

                        // Add other metadata sub-items under the package name
                        if (!pkg.author.isEmpty())
                        {
                            auto* authorItem = new RepositoryTreeItem(
                                "Author: " + pkg.author,
                                RepositoryTreeItem::ItemType::Metadata,
                                nullptr,
                                this
                            );
                            catItem->addSubItem(authorItem);
                        }
                        if (!pkg.version.isEmpty())
                        {
                            auto* versionItem = new RepositoryTreeItem(
                                "Version: " + pkg.version,
                                RepositoryTreeItem::ItemType::Metadata,
                                nullptr,
                                this
                            );
                            catItem->addSubItem(versionItem);
                        }
                        if (!pkg.description.isEmpty())
                        {
                            auto* descItem = new RepositoryTreeItem(
                                "Description: " + pkg.description,
                                RepositoryTreeItem::ItemType::Metadata,
                                nullptr,
                                this
                            );
                            catItem->addSubItem(descItem);
                        }
                    }

                    // set root to tree
                    repoTree.setRootItem(rootItem.get());
                }

                int count = --(*remaining);
                if (count == 0)
                {
                    // All repositories loaded
                    isLoading = false;

                    juce::String status = juce::String(allPackages.size())
                                        + " JSFX available from "
                                        + juce::String(repositories.size())
                                        + " repositor"
                                        + (repositories.size() == 1 ? "y" : "ies");
                    statusLabel.setText(status, juce::dontSendNotification);
                    refreshButton.setEnabled(true);
                    installAllButton.setEnabled(!allPackages.empty());

                    // Enable Install Selected button when nothing selected
                    if (!allPackages.empty())
                    {
                        installButton.setButtonText("Install Selected");
                        installButton.setEnabled(true);
                    }

                    // Force repaint to update installation status badges
                    repoTree.repaint();

                    // Check for version mismatches after loading
                    checkForVersionMismatches();
                }
            }
        );
    }
}

void RepositoryWindow::refreshPackageList()
{
    repoTree.repaint();          // Force repaint to update INSTALLED badges
    updateButtonsForSelection(); // Update button text based on installation status
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
        if (!repositoryManager.isPackageInstalled(pkg))
            continue;

        // Skip ignored packages
        if (repositoryManager.isPackageIgnored(pkg))
            continue;

        juce::String installedVersion = repositoryManager.getInstalledVersion(pkg);
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
                    if (!repositoryManager.isPackagePinned(*m.package))
                        totalToUpdate++;

                if (totalToUpdate == 0)
                {
                    statusLabel.setText("All mismatched packages are pinned", juce::dontSendNotification);
                    return;
                }

                auto packagesToUpdate = std::make_shared<std::vector<RepositoryManager::JSFXPackage>>();
                for (const auto& m : mismatches)
                    if (!repositoryManager.isPackagePinned(*m.package))
                        packagesToUpdate->push_back(*m.package);

                auto updated = std::make_shared<std::atomic<int>>(0);
                auto failed = std::make_shared<std::atomic<int>>(0);

                installButton.setEnabled(false);
                installAllButton.setEnabled(false);
                refreshButton.setEnabled(false);

                for (const auto& package : *packagesToUpdate)
                {
                    repositoryManager.installPackage(
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
                                repoTree.repaint();
                                updateButtonsForSelection();
                            }
                        }
                    );
                }
            }
        }
    );
}

void RepositoryWindow::collectSelectedRepoItems(juce::Array<RepositoryTreeItem*>& items, RepositoryTreeItem* item)
{
    if (item == nullptr)
        return;

    if (item->isSelected())
        items.add(item);

    for (int i = 0; i < item->getNumSubItems(); ++i)
        if (auto* sub = dynamic_cast<RepositoryTreeItem*>(item->getSubItem(i)))
            collectSelectedRepoItems(items, sub);
}

juce::Array<RepositoryTreeItem*> RepositoryWindow::getSelectedRepoItems()
{
    juce::Array<RepositoryTreeItem*> items;
    if (rootItem)
        collectSelectedRepoItems(items, rootItem.get());
    return items;
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
    auto selected = getSelectedRepoItems();
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
        repositoryManager.installPackage(
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
                    repoTree.repaint();
                    updateButtonsForSelection();
                }
            }
        );
    }
}

void RepositoryWindow::installAllPackages()
{
    if (allPackages.empty())
    {
        DBG("Install All: No packages available");
        return;
    }

    // Check if button says "Uninstall All"
    if (installAllButton.getButtonText() == "Uninstall All")
    {
        uninstallAllPackages();
        return;
    }

    int toInstall = static_cast<int>(allPackages.size());

    DBG("Install All: Will install/update " << toInstall << " packages");

    // Confirm installation - using async version to avoid blocking
    // Get the top-level window to ensure dialog appears in front
    auto* topLevel = getTopLevelComponent();

    DBG("Showing install confirmation dialog for " << toInstall << " packages");

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
            DBG("Dialog result: " << result << " (0=OK, 1=Cancel)");

            if (result != 0) // Not the OK button (first button = 0)
            {
                DBG("User cancelled install all");
                return;
            }

            DBG("User confirmed install all for " << toInstall << " packages");

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

    DBG("proceedWithInstallation: Installing/updating " << toInstall << " packages");

    // Disable buttons during installation
    installButton.setEnabled(false);
    installAllButton.setEnabled(false);
    refreshButton.setEnabled(false);

    statusLabel.setText("Preparing to install " + juce::String(toInstall) + " packages...", juce::dontSendNotification);

    auto totalToInstall = packagesToInstall->size();
    auto installed = std::make_shared<std::atomic<int>>(0);
    auto failed = std::make_shared<std::atomic<int>>(0);

    DBG("Starting installation loop for " << totalToInstall << " packages");

    if (totalToInstall == 0)
    {
        DBG("ERROR: totalToInstall is 0!");
        installButton.setEnabled(true);
        installAllButton.setEnabled(true);
        refreshButton.setEnabled(true);
        return;
    }

    for (size_t i = 0; i < packagesToInstall->size(); ++i)
    {
        const auto& package = (*packagesToInstall)[i];
        DBG("Queueing installation " << (i + 1) << "/" << totalToInstall << ": " << package.name);

        repositoryManager.installPackage(
            package,
            [this, package, installed, failed, totalToInstall, i](bool success, juce::String message)
            {
                DBG("Callback received for " << package.name << " (item " << (i + 1) << "/" << totalToInstall << ")");

                if (success)
                {
                    (*installed)++;
                    DBG("Successfully installed: " << package.name);
                }
                else
                {
                    (*failed)++;
                    DBG("Failed to install: " << package.name << " - " << message);
                }

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
                    DBG("All installations complete. Installed: " << *installed << ", Failed: " << *failed);

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
                    repoTree.repaint();          // Update INSTALLED badges
                    updateButtonsForSelection(); // Update button text

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

    DBG("All " << totalToInstall << " installation requests queued");
}

void RepositoryWindow::uninstallSelectedPackage()
{
    auto selected = getSelectedRepoItems();
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
                auto installDir = repositoryManager.getPackageInstallDirectory(*pkg);
                if (installDir.exists() && installDir.deleteRecursively())
                    uninstalled++;
                else
                    failed++;
            }

            repoTree.repaint();
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
        if (repositoryManager.isPackageInstalled(package))
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
                if (repositoryManager.isPackageInstalled(package))
                {
                    auto installDir = repositoryManager.getPackageInstallDirectory(package);
                    if (installDir.exists() && installDir.deleteRecursively())
                        uninstalled++;
                    else
                        failed++;
                }
            }

            repoTree.repaint();
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
        if (repositoryManager.isPackageInstalled(package))
            installedCount++;

    if (installedCount == static_cast<int>(allPackages.size()))
        installAllButton.setButtonText("Uninstall All");
    else
        installAllButton.setButtonText("Install All");
}

void RepositoryWindow::updateButtonsForSelection()
{
    auto selected = getSelectedRepoItems();

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
                if (repositoryManager.isPackageInstalled(*pkg))
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

void RepositoryWindow::installPackage(const RepositoryManager::JSFXPackage& package)
{
    statusLabel.setText("Installing " + package.name + "...", juce::dontSendNotification);
    installButton.setEnabled(false);
    installAllButton.setEnabled(false);
    refreshButton.setEnabled(false);

    repositoryManager.installPackage(
        package,
        [this, package](bool success, juce::String message)
        {
            if (success)
                statusLabel.setText("Successfully installed " + package.name, juce::dontSendNotification);
            else
                statusLabel.setText("Failed to install " + package.name + ": " + message, juce::dontSendNotification);

            installButton.setEnabled(true);
            installAllButton.setEnabled(true);
            refreshButton.setEnabled(true);
            repoTree.repaint();
            updateButtonsForSelection();
        }
    );
}

void RepositoryWindow::uninstallPackage(const RepositoryManager::JSFXPackage& package)
{
    statusLabel.setText("Uninstalling " + package.name + "...", juce::dontSendNotification);
    installButton.setEnabled(false);
    installAllButton.setEnabled(false);
    refreshButton.setEnabled(false);

    repositoryManager.uninstallPackage(
        package,
        [this, package](bool success, juce::String message)
        {
            if (success)
                statusLabel.setText("Successfully uninstalled " + package.name, juce::dontSendNotification);
            else
                statusLabel.setText("Failed to uninstall " + package.name + ": " + message, juce::dontSendNotification);

            installButton.setEnabled(true);
            installAllButton.setEnabled(true);
            refreshButton.setEnabled(true);
            repoTree.repaint();
            updateButtonsForSelection();
        }
    );
}

void RepositoryWindow::installFromTreeItem(RepositoryTreeItem* item)
{
    if (!item)
        return;

    // Collect all packages from this item and its children
    std::vector<RepositoryManager::JSFXPackage> packagesToInstall;
    int skippedPinned = 0;
    int skippedIgnored = 0;

    std::function<void(RepositoryTreeItem*)> collectPackages;
    collectPackages = [&](RepositoryTreeItem* treeItem)
    {
        if (treeItem->getType() == RepositoryTreeItem::ItemType::Package)
        {
            if (auto* pkg = treeItem->getPackage())
            {
                // Skip if already installed
                if (repositoryManager.isPackageInstalled(*pkg))
                    return;

                // Skip pinned packages (they shouldn't be auto-installed)
                if (repositoryManager.isPackagePinned(*pkg))
                {
                    skippedPinned++;
                    return;
                }

                // Skip ignored packages
                if (repositoryManager.isPackageIgnored(*pkg))
                {
                    skippedIgnored++;
                    return;
                }

                packagesToInstall.push_back(*pkg);
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

    juce::String skipMessage;
    if (skippedPinned > 0 || skippedIgnored > 0)
    {
        skipMessage = " (skipped ";
        if (skippedPinned > 0)
            skipMessage += juce::String(skippedPinned) + " pinned";
        if (skippedPinned > 0 && skippedIgnored > 0)
            skipMessage += ", ";
        if (skippedIgnored > 0)
            skipMessage += juce::String(skippedIgnored) + " ignored";
        skipMessage += ")";
    }

    if (packagesToInstall.empty())
    {
        statusLabel.setText("No packages to install" + skipMessage, juce::dontSendNotification);
        return;
    }

    // Install all collected packages
    auto packagesPtr = std::make_shared<std::vector<RepositoryManager::JSFXPackage>>(packagesToInstall);
    auto totalToInstall = packagesPtr->size();
    auto installed = std::make_shared<std::atomic<int>>(0);
    auto failed = std::make_shared<std::atomic<int>>(0);

    installButton.setEnabled(false);
    installAllButton.setEnabled(false);
    refreshButton.setEnabled(false);

    auto skipMessageCopy = std::make_shared<juce::String>(skipMessage);

    for (const auto& package : *packagesPtr)
    {
        repositoryManager.installPackage(
            package,
            [this, installed, failed, totalToInstall, skipMessageCopy](bool success, juce::String message)
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
                            + " failed"
                            + *skipMessageCopy,
                        juce::dontSendNotification
                    );

                    installButton.setEnabled(true);
                    installAllButton.setEnabled(true);
                    refreshButton.setEnabled(true);
                    repoTree.repaint();
                    updateButtonsForSelection();
                }
            }
        );
    }
}

void RepositoryWindow::uninstallFromTreeItem(RepositoryTreeItem* item)
{
    if (!item)
        return;

    // Collect all installed packages from this item and its children
    std::vector<RepositoryManager::JSFXPackage> packagesToUninstall;
    int skippedPinned = 0;
    int skippedIgnored = 0;

    std::function<void(RepositoryTreeItem*)> collectPackages;
    collectPackages = [&](RepositoryTreeItem* treeItem)
    {
        if (treeItem->getType() == RepositoryTreeItem::ItemType::Package)
        {
            if (auto* pkg = treeItem->getPackage())
            {
                // Skip if not installed
                if (!repositoryManager.isPackageInstalled(*pkg))
                    return;

                // Skip pinned packages (they shouldn't be auto-uninstalled)
                if (repositoryManager.isPackagePinned(*pkg))
                {
                    skippedPinned++;
                    return;
                }

                // Skip ignored packages
                if (repositoryManager.isPackageIgnored(*pkg))
                {
                    skippedIgnored++;
                    return;
                }

                packagesToUninstall.push_back(*pkg);
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

    juce::String skipMessage;
    if (skippedPinned > 0 || skippedIgnored > 0)
    {
        skipMessage = " (skipped ";
        if (skippedPinned > 0)
            skipMessage += juce::String(skippedPinned) + " pinned";
        if (skippedPinned > 0 && skippedIgnored > 0)
            skipMessage += ", ";
        if (skippedIgnored > 0)
            skipMessage += juce::String(skippedIgnored) + " ignored";
        skipMessage += ")";
    }

    if (packagesToUninstall.empty())
    {
        statusLabel.setText("No packages to uninstall" + skipMessage, juce::dontSendNotification);
        return;
    }

    // Uninstall all collected packages
    auto packagesPtr = std::make_shared<std::vector<RepositoryManager::JSFXPackage>>(packagesToUninstall);
    auto totalToUninstall = packagesPtr->size();
    auto uninstalled = std::make_shared<std::atomic<int>>(0);
    auto failed = std::make_shared<std::atomic<int>>(0);

    installButton.setEnabled(false);
    installAllButton.setEnabled(false);
    refreshButton.setEnabled(false);

    auto skipMessageCopy = std::make_shared<juce::String>(skipMessage);

    for (const auto& package : *packagesPtr)
    {
        repositoryManager.uninstallPackage(
            package,
            [this, uninstalled, failed, totalToUninstall, skipMessageCopy](bool success, juce::String message)
            {
                if (success)
                    (*uninstalled)++;
                else
                    (*failed)++;

                int completed = (*uninstalled) + (*failed);
                statusLabel.setText(
                    "Uninstalling... " + juce::String(completed) + "/" + juce::String(totalToUninstall),
                    juce::dontSendNotification
                );

                if (completed >= static_cast<int>(totalToUninstall))
                {
                    statusLabel.setText(
                        "Uninstall complete: "
                            + juce::String(*uninstalled)
                            + " uninstalled, "
                            + juce::String(*failed)
                            + " failed"
                            + *skipMessageCopy,
                        juce::dontSendNotification
                    );

                    installButton.setEnabled(true);
                    installAllButton.setEnabled(true);
                    refreshButton.setEnabled(true);
                    repoTree.repaint();
                    updateButtonsForSelection();
                }
            }
        );
    }
}

void RepositoryWindow::togglePackagePinned(const RepositoryManager::JSFXPackage& package)
{
    bool currentlyPinned = repositoryManager.isPackagePinned(package);
    repositoryManager.setPackagePinned(package, !currentlyPinned);

    statusLabel.setText(package.name + (currentlyPinned ? " unpinned" : " pinned"), juce::dontSendNotification);

    repoTree.repaint();
}

void RepositoryWindow::togglePackageIgnored(const RepositoryManager::JSFXPackage& package)
{
    bool currentlyIgnored = repositoryManager.isPackageIgnored(package);
    repositoryManager.setPackageIgnored(package, !currentlyIgnored);

    statusLabel.setText(package.name + (currentlyIgnored ? " unignored" : " ignored"), juce::dontSendNotification);

    repoTree.repaint();
}

void RepositoryWindow::pinAllFromTreeItem(RepositoryTreeItem* item)
{
    if (!item)
        return;

    int pinned = 0;

    std::function<void(RepositoryTreeItem*)> pinPackages;
    pinPackages = [&](RepositoryTreeItem* treeItem)
    {
        if (treeItem->getType() == RepositoryTreeItem::ItemType::Package)
        {
            if (auto* pkg = treeItem->getPackage())
            {
                if (!repositoryManager.isPackagePinned(*pkg))
                {
                    repositoryManager.setPackagePinned(*pkg, true);
                    pinned++;
                }
            }
        }
        else
        {
            for (int i = 0; i < treeItem->getNumSubItems(); ++i)
                if (auto* child = dynamic_cast<RepositoryTreeItem*>(treeItem->getSubItem(i)))
                    pinPackages(child);
        }
    };

    pinPackages(item);

    statusLabel.setText(
        juce::String(pinned) + " package" + (pinned == 1 ? "" : "s") + " pinned",
        juce::dontSendNotification
    );
    repoTree.repaint();
}

void RepositoryWindow::unpinAllFromTreeItem(RepositoryTreeItem* item)
{
    if (!item)
        return;

    int unpinned = 0;

    std::function<void(RepositoryTreeItem*)> unpinPackages;
    unpinPackages = [&](RepositoryTreeItem* treeItem)
    {
        if (treeItem->getType() == RepositoryTreeItem::ItemType::Package)
        {
            if (auto* pkg = treeItem->getPackage())
            {
                if (repositoryManager.isPackagePinned(*pkg))
                {
                    repositoryManager.setPackagePinned(*pkg, false);
                    unpinned++;
                }
            }
        }
        else
        {
            for (int i = 0; i < treeItem->getNumSubItems(); ++i)
                if (auto* child = dynamic_cast<RepositoryTreeItem*>(treeItem->getSubItem(i)))
                    unpinPackages(child);
        }
    };

    unpinPackages(item);

    statusLabel.setText(
        juce::String(unpinned) + " package" + (unpinned == 1 ? "" : "s") + " unpinned",
        juce::dontSendNotification
    );
    repoTree.repaint();
}

void RepositoryWindow::ignoreAllFromTreeItem(RepositoryTreeItem* item)
{
    if (!item)
        return;

    int ignored = 0;

    std::function<void(RepositoryTreeItem*)> ignorePackages;
    ignorePackages = [&](RepositoryTreeItem* treeItem)
    {
        if (treeItem->getType() == RepositoryTreeItem::ItemType::Package)
        {
            if (auto* pkg = treeItem->getPackage())
            {
                if (!repositoryManager.isPackageIgnored(*pkg))
                {
                    repositoryManager.setPackageIgnored(*pkg, true);
                    ignored++;
                }
            }
        }
        else
        {
            for (int i = 0; i < treeItem->getNumSubItems(); ++i)
                if (auto* child = dynamic_cast<RepositoryTreeItem*>(treeItem->getSubItem(i)))
                    ignorePackages(child);
        }
    };

    ignorePackages(item);

    statusLabel.setText(
        juce::String(ignored) + " package" + (ignored == 1 ? "" : "s") + " ignored",
        juce::dontSendNotification
    );
    repoTree.repaint();
}

void RepositoryWindow::unignoreAllFromTreeItem(RepositoryTreeItem* item)
{
    if (!item)
        return;

    int unignored = 0;

    std::function<void(RepositoryTreeItem*)> unignorePackages;
    unignorePackages = [&](RepositoryTreeItem* treeItem)
    {
        if (treeItem->getType() == RepositoryTreeItem::ItemType::Package)
        {
            if (auto* pkg = treeItem->getPackage())
            {
                if (repositoryManager.isPackageIgnored(*pkg))
                {
                    repositoryManager.setPackageIgnored(*pkg, false);
                    unignored++;
                }
            }
        }
        else
        {
            for (int i = 0; i < treeItem->getNumSubItems(); ++i)
                if (auto* child = dynamic_cast<RepositoryTreeItem*>(treeItem->getSubItem(i)))
                    unignorePackages(child);
        }
    };

    unignorePackages(item);

    statusLabel.setText(
        juce::String(unignored) + " package" + (unignored == 1 ? "" : "s") + " unignored",
        juce::dontSendNotification
    );
    repoTree.repaint();
}

void RepositoryWindow::showRepositoryEditor()
{
    auto* editor = new RepositoryEditorDialog(
        repositoryManager,
        [this]()
        {
            // Refresh after editing
            refreshRepositoryList();
        }
    );

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(editor);
    options.dialogTitle = "Manage Repositories";
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);

    options.launchAsync();
}

//==============================================================================
// RepositoryEditorDialog
//==============================================================================

RepositoryEditorDialog::RepositoryEditorDialog(RepositoryManager& repoManager, std::function<void()> onClose)
    : repositoryManager(repoManager)
    , closeCallback(onClose)
{
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

RepositoryEditorDialog::~RepositoryEditorDialog() = default;

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
