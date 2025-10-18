#pragma once

#include "SearchableTreeView.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Forward declarations
class AudioPluginAudioProcessor;
class PresetTreeView;

/**
 * @brief Tree item for preset browser
 * Hierarchy: Directory > File > Bank > Preset
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

    void paintItem(juce::Graphics& g, int width, int height) override;
    void paintOpenCloseButton(
        juce::Graphics& g,
        const juce::Rectangle<float>& area,
        juce::Colour backgroundColour,
        bool isMouseOver
    ) override;
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
 * Displays preset files in hierarchical structure:
 * - Directory (scan root)
 *   - File (.rpl file)
 *     - Bank (preset bank/category)
 *       - Preset (individual preset)
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

    // Load presets from directory paths
    void loadPresets(const juce::StringArray& directoryPaths);

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
    };

    std::vector<DirectoryEntry> presetDirectories;

    // Helper methods
    void parsePresetFile(const juce::File& file, FileEntry& fileEntry);
    void collectSelectedPresetItems(juce::Array<PresetTreeItem*>& items, juce::TreeViewItem* item);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetTreeView)
};
