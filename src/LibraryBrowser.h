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
     * Parameters: category, label, itemData
     */
    using ItemSelectedCallback =
        std::function<void(const juce::String& category, const juce::String& label, const juce::String& itemData)>;

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
    // Custom TextEditor that allows Down arrow to pass through when popup is visible
    class SearchTextEditor : public juce::TextEditor
    {
    public:
        SearchTextEditor(LibraryBrowser& owner)
            : owner(owner)
        {
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            // If Down arrow and popup is visible, transfer focus to popup
            if (key == juce::KeyPress::downKey)
            {
                if (owner.filteredPopup && owner.filteredPopup->isVisible())
                {
                    owner.filteredPopup->grabKeyboardFocus();
                    return true;
                }
            }

            // Otherwise, handle normally
            return juce::TextEditor::keyPressed(key);
        }

    private:
        LibraryBrowser& owner;
    };

    // Custom LookAndFeel for multi-column hierarchical menu
    class BrowserLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override;
    };

    // Custom non-modal filtered list popup
    class FilteredListPopup : public juce::Component
    {
    public:
        struct Item
        {
            juce::String category; // Parent/group name (e.g., bank, folder)
            juce::String label;    // Item display name
            int index;             // Index in flat list
            bool isHeader;         // Whether this is a category header
        };

        FilteredListPopup(LibraryBrowser& owner);
        void setItems(const std::vector<Item>& items);
        void show(juce::Component& attachTo);
        void hide();
        bool isVisible() const;
        void selectNext();
        void selectPrevious();
        void selectCurrent();

        int getSelectedIndex() const
        {
            return selectedIndex;
        }

        void paint(juce::Graphics& g) override;
        void resized() override;
        bool keyPressed(const juce::KeyPress& key) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    private:
        LibraryBrowser& owner;
        std::vector<Item> itemList;
        int selectedIndex = -1;
        int hoveredIndex = -1;
        int itemHeight = 20;
        int scrollOffset = 0;  // Y offset for scrolling
        int contentHeight = 0; // Total height of all items
        int idealWidth = 0;    // Calculated width based on content

        void ensureSelectedVisible();
    };

    void buildHierarchicalMenu(juce::PopupMenu& menu);
    void buildFilteredList(std::vector<FilteredListPopup::Item>& items, const juce::String& searchText);
    void showHierarchicalPopup();
    void showFilteredPopup(const juce::String& searchText);
    void onMenuResult(int result);
    void onFilteredItemSelected(int index);
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
    SearchTextEditor textEditor;
    juce::TextButton dropdownButton;
    BrowserLookAndFeel lookAndFeel;
    std::unique_ptr<FilteredListPopup> filteredPopup;

    juce::String currentItemName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryBrowser)
};
