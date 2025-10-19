#include "RepositoryTreeView.h"

// ============================================================================
// RepositoryTreeItem implementation
// ============================================================================

RepositoryTreeItem::RepositoryTreeItem(
    const juce::String& name,
    ItemType t,
    const RepositoryManager::JSFXPackage* pkg,
    RepositoryTreeView* view
)
    : itemName(name)
    , type(t)
    , package(pkg)
    , repositoryTreeView(view)
{
    if (view)
        repositoryManager = &view->getRepositoryManager();
}

bool RepositoryTreeItem::mightContainSubItems()
{
    // Only Index and Category items show the triangle (they have real child items)
    // Packages and Metadata never show a triangle
    return type == ItemType::Index || type == ItemType::Category;
}

bool RepositoryTreeItem::canBeSelected() const
{
    // Index, Category, and Package items can be selected
    return type == ItemType::Index || type == ItemType::Category || type == ItemType::Package;
}

void RepositoryTreeItem::itemSelectionChanged(bool isNowSelected)
{
    // If a metadata item somehow gets selected, immediately deselect it
    if (type == ItemType::Metadata && isNowSelected)
    {
        setSelected(false, false);
        return;
    }

    // Call base implementation for other item types
    juce::TreeViewItem::itemSelectionChanged(isNowSelected);
}

void RepositoryTreeItem::paintOpenCloseButton(
    juce::Graphics& g,
    const juce::Rectangle<float>& area,
    juce::Colour backgroundColour,
    bool isMouseOver
)
{
    // For packages, don't draw anything (no triangle)
    if (type == ItemType::Package)
        return;

    // For other types, use default behavior
    juce::TreeViewItem::paintOpenCloseButton(g, area, backgroundColour, isMouseOver);
}

void RepositoryTreeItem::paintItem(juce::Graphics& g, int width, int height)
{
    // Draw match highlight
    paintMatchHighlight(g, width, height);

    g.setColour(type == ItemType::Metadata ? juce::Colours::grey : juce::Colours::white);
    g.setFont(juce::Font(type == ItemType::Metadata ? 11.0f : 14.0f));

    // Use standard offset
    int xOffset = 4;

    // Main item name
    g.drawText(itemName, xOffset, 0, width - 100, height, juce::Justification::centredLeft, true);

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

void RepositoryTreeItem::itemClicked(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && repositoryTreeView)
    {
        juce::PopupMenu menu;

        // Only show menu for Package, Category, or Index items
        if (type == ItemType::Metadata)
            return;

        // Get all selected items (including this one)
        auto selectedItems = repositoryTreeView->getSelectedRepoItems();

        // If this item isn't selected, treat it as a single-item selection
        if (!selectedItems.contains(this))
        {
            selectedItems.clear();
            selectedItems.add(this);
        }

        bool hasMultipleItems = selectedItems.size() > 1;

        // Analyze the selected items to determine states
        bool hasInstalledPackage = false;
        bool hasUninstalledPackage = false;
        bool hasPinnedPackage = false;
        bool hasUnpinnedPackage = false;
        bool hasIgnoredPackage = false;
        bool hasUnignoredPackage = false;

        std::function<void(RepositoryTreeItem*)> analyzeItem;
        analyzeItem = [&](RepositoryTreeItem* item)
        {
            if (item->getType() == ItemType::Package && item->getPackage() && repositoryManager)
            {
                auto* pkg = item->getPackage();
                if (repositoryManager->isPackageInstalled(*pkg))
                    hasInstalledPackage = true;
                else
                    hasUninstalledPackage = true;

                if (repositoryManager->isPackagePinned(*pkg))
                    hasPinnedPackage = true;
                else
                    hasUnpinnedPackage = true;

                if (repositoryManager->isPackageIgnored(*pkg))
                    hasIgnoredPackage = true;
                else
                    hasUnignoredPackage = true;
            }

            // Recurse into children for Category/Index items
            if (item->getType() != ItemType::Package && item->getType() != ItemType::Metadata)
            {
                for (int i = 0; i < item->getNumSubItems(); ++i)
                    if (auto* sub = dynamic_cast<RepositoryTreeItem*>(item->getSubItem(i)))
                        analyzeItem(sub);
            }
        };

        for (auto* item : selectedItems)
            analyzeItem(item);

        // Build menu based on what operations are available
        if (hasUninstalledPackage)
            menu.addItem(2, hasMultipleItems ? "Install" : "Install");
        if (hasInstalledPackage)
            menu.addItem(1, hasMultipleItems ? "Uninstall" : "Uninstall");

        if (hasUninstalledPackage || hasInstalledPackage)
            menu.addSeparator();

        if (hasUnpinnedPackage)
            menu.addItem(7, hasMultipleItems ? "Pin All" : "Pin");
        if (hasPinnedPackage)
            menu.addItem(8, hasMultipleItems ? "Unpin All" : "Unpin");

        if (hasPinnedPackage || hasUnpinnedPackage)
            menu.addSeparator();

        if (hasUnignoredPackage)
            menu.addItem(9, hasMultipleItems ? "Ignore All" : "Ignore");
        if (hasIgnoredPackage)
            menu.addItem(10, hasMultipleItems ? "Unignore All" : "Unignore");

        auto options = juce::PopupMenu::Options();
        menu.showMenuAsync(
            options,
            [this, selectedItems](int result)
            {
                if (result == 0)
                    return;

                // Convert to tree items array
                juce::Array<juce::TreeViewItem*> treeItems;
                for (auto* item : selectedItems)
                    treeItems.add(item);

                switch (result)
                {
                case 1: // Uninstall
                    repositoryTreeView->uninstallFromTreeItems(treeItems);
                    break;

                case 2: // Install
                    repositoryTreeView->installFromTreeItems(treeItems);
                    break;

                case 7: // Pin
                    repositoryTreeView->pinAllFromTreeItems(treeItems);
                    break;

                case 8: // Unpin
                    repositoryTreeView->unpinAllFromTreeItems(treeItems);
                    break;

                case 9: // Ignore
                    repositoryTreeView->ignoreAllFromTreeItems(treeItems);
                    break;

                case 10: // Unignore
                    repositoryTreeView->unignoreAllFromTreeItems(treeItems);
                    break;

                default:
                    break;
                }
            }
        );
    }
}

// ============================================================================
// RepositoryTreeView implementation
// ============================================================================

RepositoryTreeView::RepositoryTreeView(RepositoryManager& repoManager)
    : repositoryManager(repoManager)
{
}

RepositoryTreeView::~RepositoryTreeView()
{
}

std::unique_ptr<juce::TreeViewItem> RepositoryTreeView::createRootItem()
{
    auto root =
        std::make_unique<RepositoryTreeItem>("Repositories", RepositoryTreeItem::ItemType::Index, nullptr, this);
    root->setRepositoryManager(&repositoryManager);

    // Build tree structure: repositories → categories → packages
    for (const auto& repo : repositories)
    {
        auto* repoItem = new RepositoryTreeItem(repo.name, RepositoryTreeItem::ItemType::Index, nullptr, this);
        repoItem->setRepositoryManager(&repositoryManager);
        root->addSubItem(repoItem);

        // Group packages by category
        std::map<juce::String, std::vector<const RepositoryManager::JSFXPackage*>> categorizedPackages;

        for (const auto& pkg : repo.packages)
            categorizedPackages[pkg.category].push_back(&pkg);

        // Add categories and packages
        for (const auto& [category, packages] : categorizedPackages)
        {
            auto* categoryItem =
                new RepositoryTreeItem(category, RepositoryTreeItem::ItemType::Category, nullptr, this);
            categoryItem->setRepositoryManager(&repositoryManager);
            repoItem->addSubItem(categoryItem);

            for (const auto* pkg : packages)
            {
                auto* packageItem = new RepositoryTreeItem(pkg->name, RepositoryTreeItem::ItemType::Package, pkg, this);
                packageItem->setRepositoryManager(&repositoryManager);
                categoryItem->addSubItem(packageItem);

                // Get metadata for this package and add as sibling items (not children)
                // This prevents the package from showing a triangle
                auto metadata = getMetadataForItem(packageItem);
                for (const auto& [label, value] : metadata)
                {
                    auto* metadataItem = new RepositoryTreeItem(
                        label + ": " + value,
                        RepositoryTreeItem::ItemType::Metadata,
                        nullptr,
                        this
                    );
                    // Add metadata as sibling to package (child of category), not as child of package
                    categoryItem->addSubItem(metadataItem);
                }
            }
        }
    }

    return root;
}

void RepositoryTreeView::onSelectionChanged()
{
    // Notify parent window
    if (onSelectionChangedCallback)
        onSelectionChangedCallback();
}

juce::Array<std::pair<juce::String, juce::String>> RepositoryTreeView::getMetadataForItem(juce::TreeViewItem* item)
{
    juce::Array<std::pair<juce::String, juce::String>> metadata;

    if (auto* repoItem = dynamic_cast<RepositoryTreeItem*>(item))
    {
        // For packages, show author, version, and description
        if (repoItem->getType() == RepositoryTreeItem::ItemType::Package)
        {
            if (auto* pkg = repoItem->getPackage())
            {
                if (!pkg->author.isEmpty())
                    metadata.add({"Author", pkg->author});

                if (!pkg->version.isEmpty())
                    metadata.add({"Version", pkg->version});

                if (!pkg->description.isEmpty())
                    metadata.add({"Description", pkg->description});
            }
        }
        // Could add metadata for other item types here in the future
        // For example, categories could show package counts, repositories could show URLs
    }

    return metadata;
}

bool RepositoryTreeView::shouldIncludeInSearch(juce::TreeViewItem* item)
{
    // Exclude metadata items from search matching
    if (auto* repoItem = dynamic_cast<RepositoryTreeItem*>(item))
    {
        if (repoItem->getType() == RepositoryTreeItem::ItemType::Metadata)
            return false;
    }
    return true;
}

void RepositoryTreeView::onEnterKeyPressed(juce::TreeViewItem* selectedItem)
{
    if (auto* repoItem = dynamic_cast<RepositoryTreeItem*>(selectedItem))
    {
        if (repoItem->getType() == RepositoryTreeItem::ItemType::Package)
        {
            if (auto* pkg = repoItem->getPackage())
            {
                // Check if package is pinned
                if (repositoryManager.isPackagePinned(*pkg))
                    return; // Can't install/uninstall pinned packages

                // Toggle install/uninstall
                if (repositoryManager.isPackageInstalled(*pkg))
                    uninstallPackage(*pkg);
                else
                    installPackage(*pkg);
            }
        }
    }
}

void RepositoryTreeView::collectSelectedRepoItems(juce::Array<RepositoryTreeItem*>& items, juce::TreeViewItem* item)
{
    if (!item)
        return;

    if (auto* repoItem = dynamic_cast<RepositoryTreeItem*>(item))
    {
        if (item->isSelected())
            items.add(repoItem);
    }

    for (int i = 0; i < item->getNumSubItems(); ++i)
        collectSelectedRepoItems(items, item->getSubItem(i));
}

juce::Array<RepositoryTreeItem*> RepositoryTreeView::getSelectedRepoItems()
{
    juce::Array<RepositoryTreeItem*> items;
    if (auto* root = getRootItem())
        collectSelectedRepoItems(items, root);
    return items;
}

// Package operations (to be implemented with actual install/uninstall logic)
void RepositoryTreeView::installPackage(const RepositoryManager::JSFXPackage& package)
{
    if (onInstallPackage)
        onInstallPackage(package);
}

void RepositoryTreeView::uninstallPackage(const RepositoryManager::JSFXPackage& package)
{
    if (onUninstallPackage)
        onUninstallPackage(package);
}

void RepositoryTreeView::togglePackagePinned(const RepositoryManager::JSFXPackage& package)
{
    bool currentlyPinned = repositoryManager.isPackagePinned(package);
    repositoryManager.setPackagePinned(package, !currentlyPinned);
    getTreeView().repaint();
}

void RepositoryTreeView::togglePackageIgnored(const RepositoryManager::JSFXPackage& package)
{
    bool currentlyIgnored = repositoryManager.isPackageIgnored(package);
    repositoryManager.setPackageIgnored(package, !currentlyIgnored);
    getTreeView().repaint();
}

void RepositoryTreeView::installFromTreeItems(const juce::Array<juce::TreeViewItem*>& items)
{
    // Collect packages from items
    std::vector<RepositoryManager::JSFXPackage> packages;

    std::function<void(juce::TreeViewItem*)> collectPackages;
    collectPackages = [&](juce::TreeViewItem* item)
    {
        if (auto* repoItem = dynamic_cast<RepositoryTreeItem*>(item))
        {
            if (repoItem->getType() == RepositoryTreeItem::ItemType::Package && repoItem->getPackage())
            {
                packages.push_back(*repoItem->getPackage());
            }
            else
            {
                // Recurse for categories and repos
                for (int i = 0; i < item->getNumSubItems(); ++i)
                    collectPackages(item->getSubItem(i));
            }
        }
    };

    for (auto* item : items)
        collectPackages(item);

    // Use batch callback if available (single confirmation for all packages)
    if (onBatchInstallPackages)
        onBatchInstallPackages(packages);
    else
    {
        // Fallback to individual callbacks (one confirmation per package)
        for (const auto& pkg : packages)
            installPackage(pkg);
    }
}

void RepositoryTreeView::uninstallFromTreeItems(const juce::Array<juce::TreeViewItem*>& items)
{
    // Similar to installFromTreeItems
    std::vector<RepositoryManager::JSFXPackage> packages;

    std::function<void(juce::TreeViewItem*)> collectPackages;
    collectPackages = [&](juce::TreeViewItem* item)
    {
        if (auto* repoItem = dynamic_cast<RepositoryTreeItem*>(item))
        {
            if (repoItem->getType() == RepositoryTreeItem::ItemType::Package && repoItem->getPackage())
                packages.push_back(*repoItem->getPackage());
            else
                for (int i = 0; i < item->getNumSubItems(); ++i)
                    collectPackages(item->getSubItem(i));
        }
    };

    for (auto* item : items)
        collectPackages(item);

    // Use batch callback if available (single confirmation for all packages)
    if (onBatchUninstallPackages)
        onBatchUninstallPackages(packages);
    else
    {
        // Fallback to individual callbacks (one confirmation per package)
        for (const auto& pkg : packages)
            uninstallPackage(pkg);
    }
}

void RepositoryTreeView::pinAllFromTreeItems(const juce::Array<juce::TreeViewItem*>& items)
{
    std::function<void(juce::TreeViewItem*)> pinPackages;
    pinPackages = [&](juce::TreeViewItem* item)
    {
        if (auto* repoItem = dynamic_cast<RepositoryTreeItem*>(item))
        {
            if (repoItem->getType() == RepositoryTreeItem::ItemType::Package && repoItem->getPackage())
                repositoryManager.setPackagePinned(*repoItem->getPackage(), true);
            else
                for (int i = 0; i < item->getNumSubItems(); ++i)
                    pinPackages(item->getSubItem(i));
        }
    };

    for (auto* item : items)
        pinPackages(item);

    getTreeView().repaint();
}

void RepositoryTreeView::unpinAllFromTreeItems(const juce::Array<juce::TreeViewItem*>& items)
{
    std::function<void(juce::TreeViewItem*)> unpinPackages;
    unpinPackages = [&](juce::TreeViewItem* item)
    {
        if (auto* repoItem = dynamic_cast<RepositoryTreeItem*>(item))
        {
            if (repoItem->getType() == RepositoryTreeItem::ItemType::Package && repoItem->getPackage())
                repositoryManager.setPackagePinned(*repoItem->getPackage(), false);
            else
                for (int i = 0; i < item->getNumSubItems(); ++i)
                    unpinPackages(item->getSubItem(i));
        }
    };

    for (auto* item : items)
        unpinPackages(item);

    getTreeView().repaint();
}

void RepositoryTreeView::ignoreAllFromTreeItems(const juce::Array<juce::TreeViewItem*>& items)
{
    std::function<void(juce::TreeViewItem*)> ignorePackages;
    ignorePackages = [&](juce::TreeViewItem* item)
    {
        if (auto* repoItem = dynamic_cast<RepositoryTreeItem*>(item))
        {
            if (repoItem->getType() == RepositoryTreeItem::ItemType::Package && repoItem->getPackage())
                repositoryManager.setPackageIgnored(*repoItem->getPackage(), true);
            else
                for (int i = 0; i < item->getNumSubItems(); ++i)
                    ignorePackages(item->getSubItem(i));
        }
    };

    for (auto* item : items)
        ignorePackages(item);

    getTreeView().repaint();
}

void RepositoryTreeView::unignoreAllFromTreeItems(const juce::Array<juce::TreeViewItem*>& items)
{
    std::function<void(juce::TreeViewItem*)> unignorePackages;
    unignorePackages = [&](juce::TreeViewItem* item)
    {
        if (auto* repoItem = dynamic_cast<RepositoryTreeItem*>(item))
        {
            if (repoItem->getType() == RepositoryTreeItem::ItemType::Package && repoItem->getPackage())
                repositoryManager.setPackageIgnored(*repoItem->getPackage(), false);
            else
                for (int i = 0; i < item->getNumSubItems(); ++i)
                    unignorePackages(item->getSubItem(i));
        }
    };

    for (auto* item : items)
        unignorePackages(item);

    getTreeView().repaint();
}
