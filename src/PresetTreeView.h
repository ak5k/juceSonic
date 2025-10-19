#pragma once

#include "SearchableTreeView.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Forward declarations
class AudioPluginAudioProcessor;
class PresetTreeView;

/**
 * @brief Tree item for preset browser
 * Supports flexible hierarchical structure for organizing presets
 */
class PresetTreeItem : public SearchableTreeItem
{
public:
    enum class ItemType
    {
        Directory, // Root directory being scanned
        File,      // .rpl preset file
        Bank,      // Bank within a file
        Preset     // Individual preset
    };

    PresetTreeItem(
        const juce::String& name,
        ItemType t,
        const juce::File& file = juce::File(),
        const juce::String& bankName = {},
        const juce::String& presetName = {},
        const juce::String& presetData = {},
        PresetTreeView* view = nullptr
    );

    // SearchableTreeItem overrides
    juce::String getName() const override
    {
        return itemName;
    }

    bool mightContainSubItems() override;
    bool canBeSelected() const override;
    void itemDoubleClicked(const juce::MouseEvent& e) override;
    void itemSelectionChanged(bool isNowSelected) override;

    // Accessors
    ItemType getType() const
    {
        return type;
    }

    const juce::File& getFile() const
    {
        return presetFile;
    }

    const juce::String& getBankName() const
    {
        return bank;
    }

    const juce::String& getPresetName() const
    {
        return preset;
    }

    const juce::String& getPresetData() const
    {
        return data;
    }

    void setPresetTreeView(PresetTreeView* view)
    {
        presetTreeView = view;
    }

private:
    juce::String itemName;
    ItemType type;
    juce::File presetFile; // For File, Bank, and Preset items
    juce::String bank;     // For Bank and Preset items
    juce::String preset;   // For Preset items
    juce::String data;     // Base64 preset data for Preset items
    PresetTreeView* presetTreeView = nullptr;
};

/**
 * @brief Searchable tree view for JSFX presets
 *
 * Displays preset files in flexible hierarchical structure with automatic
 * organization based on directory structure and preset file contents.
 * Supports unlimited nesting depth.
 *
 * No metadata display or command callback needed.
 */
class PresetTreeView : public SearchableTreeView
{
public:
    explicit PresetTreeView(AudioPluginAudioProcessor& proc);
    ~PresetTreeView() override;

    // Callbacks
    std::function<void()> onSelectionChangedCallback;

    // Load presets from directory paths (legacy method, kept for compatibility)
    void loadPresets(const juce::StringArray& directoryPaths);

    // Load presets from APVTS ValueTree (preferred method)
    void loadPresetsFromValueTree(const juce::ValueTree& presetsNode);

    // Get selected items for operations
    juce::Array<PresetTreeItem*> getSelectedPresetItems();

    // Apply preset to processor
    void applyPreset(const juce::String& base64Data);

    // SearchableTreeView overrides
    std::unique_ptr<juce::TreeViewItem> createRootItem() override;
    void onSelectionChanged() override;
    void onEnterKeyPressed(juce::TreeViewItem* selectedItem) override;

    juce::String getSearchPlaceholder() const override
    {
        return "Type to search presets...";
    }

    // No metadata or command callback needed
    juce::Array<std::pair<juce::String, juce::String>> getMetadataForItem(juce::TreeViewItem* item) override
    {
        return {};
    }

    bool shouldIncludeInSearch(juce::TreeViewItem* item) override;

    bool shouldCountItem(juce::TreeViewItem* item) override
    {
        // Only count actual preset items, not directories, files, or banks
        if (auto* presetItem = dynamic_cast<PresetTreeItem*>(item))
            return presetItem->getType() == PresetTreeItem::ItemType::Preset;
        return false;
    }

    // Get processor reference
    AudioPluginAudioProcessor& getProcessor()
    {
        return processor;
    }

private:
    AudioPluginAudioProcessor& processor;

    // Preset data structure
    struct PresetEntry
    {
        juce::File file;
        juce::String bank;
        juce::String preset;
        juce::String data; // Base64 encoded
    };

    struct BankEntry
    {
        juce::String bankName;
        std::vector<PresetEntry> presets;
    };

    struct FileEntry
    {
        juce::File file;
        std::vector<BankEntry> banks;
    };

    struct DirectoryEntry
    {
        juce::File directory;
        std::vector<FileEntry> files;
        bool isDefaultRoot = false;  // True if this is the default install location
        bool isExternalRoot = false; // True if this is an external JSFX directory
        bool isRemoteRoot = false;   // True if this is from remote/ directory
    };

    std::vector<DirectoryEntry> presetDirectories;

    // Helper methods
    void parsePresetFile(const juce::File& file, FileEntry& fileEntry);
    void collectSelectedPresetItems(juce::Array<PresetTreeItem*>& items, juce::TreeViewItem* item);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetTreeView)
};
