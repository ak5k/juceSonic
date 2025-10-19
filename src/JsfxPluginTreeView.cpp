#include "JsfxPluginTreeView.h"
#include "PluginProcessor.h"

//==============================================================================
// JsfxPluginTreeItem Implementation
//==============================================================================

JsfxPluginTreeItem::JsfxPluginTreeItem(
    const juce::String& name,
    ItemType t,
    const juce::File& file,
    JsfxPluginTreeView* view
)
    : itemName(name)
    , type(t)
    , pluginFile(file)
    , pluginTreeView(view)
{
}

bool JsfxPluginTreeItem::mightContainSubItems()
{
    return type == ItemType::Category; // Only categories contain sub-items
}

bool JsfxPluginTreeItem::canBeSelected() const
{
    return true; // All items can be selected
}

void JsfxPluginTreeItem::itemDoubleClicked(const juce::MouseEvent&)
{
    // Execute command (load plugin) on double-click
    if (type == ItemType::Plugin && pluginTreeView && pluginFile.existsAsFile())
    {
        juce::Array<juce::TreeViewItem*> items;
        items.add(this);
        pluginTreeView->executeCommand(items);
    }
}

void JsfxPluginTreeItem::itemSelectionChanged(bool isNowSelected)
{
    repaintItem();
}

//==============================================================================
// JsfxPluginTreeView Implementation
//==============================================================================

JsfxPluginTreeView::JsfxPluginTreeView(AudioPluginAudioProcessor& proc)
    : processor(proc)
{
}

JsfxPluginTreeView::~JsfxPluginTreeView() = default;

void JsfxPluginTreeView::loadPlugins(const juce::StringArray& directoryPaths)
{
    categories.clear();

    // Define standard categories
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    auto juceSonicDataDir =
        appDataDir.getChildFile(PluginConstants::ApplicationName).getChildFile(PluginConstants::DataDirectoryName);

    // User category: <appdata>/juceSonic/data/user
    CategoryEntry userCategory;
    userCategory.displayName = "User";
    userCategory.directory = juceSonicDataDir.getChildFile(PluginConstants::UserPresetsDirectoryName);

    // Local category: <appdata>/juceSonic/data/local
    CategoryEntry localCategory;
    localCategory.displayName = "Local";
    localCategory.directory = juceSonicDataDir.getChildFile(PluginConstants::LocalPresetsDirectoryName);

    // Remote category: <appdata>/juceSonic/data/remote
    CategoryEntry remoteCategory;
    remoteCategory.displayName = "Remote";
    remoteCategory.directory = juceSonicDataDir.getChildFile(PluginConstants::RemotePresetsDirectoryName);

    // REAPER category: <appdata>/REAPER/Effects
    CategoryEntry reaperCategory;
    reaperCategory.displayName = "REAPER";
    reaperCategory.directory = appDataDir.getChildFile("REAPER").getChildFile("Effects");

    // Add standard categories
    categories.add(userCategory);
    categories.add(localCategory);
    categories.add(remoteCategory);
    categories.add(reaperCategory);

    // Add additional custom directories from user preferences
    for (const auto& path : directoryPaths)
    {
        juce::File dir(path);
        if (!dir.exists() || !dir.isDirectory())
            continue;

        // Check if this directory is already in our standard categories
        bool isStandardCategory = false;
        for (const auto& cat : categories)
        {
            if (cat.directory == dir)
            {
                isStandardCategory = true;
                break;
            }
        }

        if (!isStandardCategory)
        {
            CategoryEntry customCategory;
            customCategory.displayName = dir.getFileName();
            customCategory.directory = dir;
            categories.add(customCategory);
        }
    }

    // Rebuild tree
    refreshTree();
}

void JsfxPluginTreeView::scanDirectory(JsfxPluginTreeItem* parentItem, const juce::File& directory, bool recursive)
{
    if (!directory.exists() || !directory.isDirectory())
        return;

    auto files = directory.findChildFiles(juce::File::findFiles, recursive, "*.jsfx");

    for (const auto& file : files)
    {
        auto pluginItem = std::make_unique<JsfxPluginTreeItem>(
            file.getFileNameWithoutExtension(),
            JsfxPluginTreeItem::ItemType::Plugin,
            file,
            this
        );

        parentItem->addSubItem(pluginItem.release());
    }
}

juce::Array<JsfxPluginTreeItem*> JsfxPluginTreeView::getSelectedPluginItems()
{
    juce::Array<JsfxPluginTreeItem*> items;
    if (auto* root = getRootItem())
        collectSelectedPluginItems(items, root);
    return items;
}

void JsfxPluginTreeView::collectSelectedPluginItems(juce::Array<JsfxPluginTreeItem*>& items, juce::TreeViewItem* item)
{
    if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
    {
        if (pluginItem->isSelected())
            items.add(pluginItem);
    }

    for (int i = 0; i < item->getNumSubItems(); ++i)
        collectSelectedPluginItems(items, item->getSubItem(i));
}

void JsfxPluginTreeView::loadPlugin(const juce::File& pluginFile)
{
    if (!pluginFile.existsAsFile())
        return;

    processor.loadJSFX(pluginFile);
}

std::unique_ptr<juce::TreeViewItem> JsfxPluginTreeView::createRootItem()
{
    auto root = std::make_unique<JsfxPluginTreeItem>("Root", JsfxPluginTreeItem::ItemType::Category);

    // Create category items and scan directories
    for (const auto& category : categories)
    {
        auto categoryItem = std::make_unique<JsfxPluginTreeItem>(
            category.displayName,
            JsfxPluginTreeItem::ItemType::Category,
            juce::File(),
            this
        );

        // Scan category directory recursively
        scanDirectory(categoryItem.get(), category.directory, true);

        // Only add category if it has plugins
        if (categoryItem->getNumSubItems() > 0)
            root->addSubItem(categoryItem.release());
    }

    return root;
}

void JsfxPluginTreeView::onSelectionChanged()
{
    if (onSelectionChangedCallback)
        onSelectionChangedCallback();
}

void JsfxPluginTreeView::onEnterKeyPressed(juce::TreeViewItem* selectedItem)
{
    if (!selectedItem)
        return;

    if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(selectedItem))
    {
        if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin)
            loadPlugin(pluginItem->getFile());
    }
}

void JsfxPluginTreeView::onBrowseMenuItemSelected(juce::TreeViewItem* selectedItem)
{
    if (!selectedItem)
        return;

    if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(selectedItem))
    {
        if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin)
            loadPlugin(pluginItem->getFile());
    }
}

bool JsfxPluginTreeView::shouldIncludeInSearch(juce::TreeViewItem* item)
{
    // Only search plugin items, not categories
    if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
        return pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin;
    return false;
}

juce::Array<juce::TreeViewItem*> JsfxPluginTreeView::getDeepestLevelItems()
{
    juce::Array<juce::TreeViewItem*> items;

    if (!getRootItem())
        return items;

    // Collect all plugin items from all categories
    std::function<void(juce::TreeViewItem*)> collectPlugins = [&](juce::TreeViewItem* item)
    {
        if (auto* pluginItem = dynamic_cast<JsfxPluginTreeItem*>(item))
        {
            if (pluginItem->getType() == JsfxPluginTreeItem::ItemType::Plugin)
            {
                items.add(item);
                return;
            }
        }

        for (int i = 0; i < item->getNumSubItems(); ++i)
            collectPlugins(item->getSubItem(i));
    };

    collectPlugins(getRootItem());
    return items;
}

juce::String JsfxPluginTreeView::getParentCategoryForItem(juce::TreeViewItem* item)
{
    if (!item)
        return {};

    // Walk up the tree to find the category
    auto* parent = item->getParentItem();
    while (parent != nullptr && parent != getRootItem())
    {
        if (auto* categoryItem = dynamic_cast<JsfxPluginTreeItem*>(parent))
        {
            if (categoryItem->getType() == JsfxPluginTreeItem::ItemType::Category)
                return categoryItem->getName();
        }
        parent = parent->getParentItem();
    }

    return {};
}
