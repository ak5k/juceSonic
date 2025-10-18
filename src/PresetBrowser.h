#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PresetManager.h"

// Forward declarations
class PresetTreeItem;

/**
 * @brief Window for browsing and managing presets
 *
 * Provides a tree view of all available presets with options to:
 * - Import presets from disk
 * - Export selected presets (individual, bank, or all)
 * - Delete selected presets
 */
class PresetBrowserWindow : public juce::Component
{
public:
    explicit PresetBrowserWindow(PresetManager& presetMgr, const juce::String& currentJsfxPath);
    ~PresetBrowserWindow() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void refreshPresetTree();
    void importPresets();
    void exportSelected();
    void deleteSelected();

    // Helper methods
    void collectSelectedItems(juce::Array<PresetTreeItem*>& items, PresetTreeItem* item);
    juce::Array<PresetTreeItem*> getSelectedItems();

    PresetManager& presetManager;
    juce::String jsfxPath;

    // UI Components
    juce::Label titleLabel;
    juce::TreeView presetTree;
    std::unique_ptr<PresetTreeItem> rootItem;

    juce::TextButton importButton;
    juce::TextButton exportButton;
    juce::TextButton deleteButton;
};

/**
 * @brief Tree view item for displaying preset files, banks, and presets
 */
class PresetTreeItem : public juce::TreeViewItem
{
public:
    enum class ItemType
    {
        Root,
        File,
        Bank,
        Preset
    };

    PresetTreeItem(const juce::String& name, ItemType type, const juce::String& filePath = {});

    bool mightContainSubItems() override;
    void paintItem(juce::Graphics& g, int width, int height) override;

    ItemType getType() const
    {
        return type;
    }

    juce::String getFilePath() const
    {
        return filePath;
    }

    juce::String getName() const
    {
        return itemName;
    }

    juce::String getItemName() const
    {
        return itemName;
    }

private:
    juce::String itemName;
    ItemType type;
    juce::String filePath;
};
