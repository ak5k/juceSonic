#pragma once

#include "LibraryManager.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/**
 * @brief Generic searchable library browser component
 *
 * A self-contained component for browsing hierarchical library data:
 * - TextEditor for typing/searching
 * - Dropdown button for full hierarchical menu
 * - Smart PopupMenu (filtered when searching, hierarchical from button)
 *
 * Can be used for presets, samples, or any hierarchical data.
 */
class LibraryBrowser : public juce::Component
{
public:
    /**
     * @brief Callback type for item selection
     * Parameters: bankName, itemName, itemData
     */
    using ItemSelectedCallback =
        std::function<void(const juce::String& bankName, const juce::String& itemName, const juce::String& itemData)>;

    LibraryBrowser();
    ~LibraryBrowser() override;

    void setLibraryManager(LibraryManager* manager);
    void setLibraryName(const juce::String& name);
    void setItemSelectedCallback(ItemSelectedCallback callback);
    void updateItemList();
    void setLabelText(const juce::String& text);
    void setPlaceholderText(const juce::String& text);

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // Custom LookAndFeel for multi-column hierarchical menu
    class BrowserLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override;
    };

    void buildHierarchicalMenu(juce::PopupMenu& menu);
    void buildFilteredMenu(juce::PopupMenu& menu, const juce::String& searchText);
    void showHierarchicalPopup();
    void showFilteredPopup(const juce::String& searchText);
    void onMenuResult(int result);
    void onSearchTextChanged();

    // Item lookup structure (generic - works for any hierarchical data)
    struct ItemIndex
    {
        int fileIdx;
        int bankIdx;
        int itemIdx;
    };

    std::vector<ItemIndex> itemIndices;

    LibraryManager* libraryManager = nullptr;
    juce::String libraryName;
    ItemSelectedCallback itemSelectedCallback;

    juce::Label label;
    juce::TextEditor textEditor;
    juce::TextButton dropdownButton;
    BrowserLookAndFeel lookAndFeel;

    juce::String currentItemName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryBrowser)
};
