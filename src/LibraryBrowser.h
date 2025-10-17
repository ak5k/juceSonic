#pragma once

#include "PresetConverter.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <memory>

/**
 * @brief Generic searchable library browser component with integrated ValueTree management
 *
 * A self-contained component for browsing hierarchical library data:
 * - TextEditor for typing/searching
 * - Dropdown button for full hierarchical menu
 * - Smart PopupMenu (filtered when searching, hierarchical from button)
 * - Direct ValueTree management without external manager
 *
 * ValueTree structure:
 *   libraryTree (root node for this browser)
 *     - children: FileNode from converter
 *       - FileNode (from converter)
 *         - CategoryNode (from converter)
 *           - ChildNode (from converter, with "data" property)
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

    /**
     * @brief Attach to a ValueTree for persistent storage
     * @param stateTree Parent ValueTree (e.g., from APVTS)
     * @param propertyName Property name to store library data under
     */
    void attachToValueTree(juce::ValueTree stateTree, const juce::Identifier& propertyName);

    /**
     * @brief Set the converter for parsing preset files
     * @param converter Converter instance (ownership transferred)
     */
    void setConverter(std::unique_ptr<PresetConverter> converter);

    /**
     * @brief Load library from directory path(s)
     * @param directoryPath Single directory path to scan
     * @param recursive Whether to scan subdirectories
     * @param clearExisting If true, clears existing data before loading
     * @return Number of files successfully loaded
     */
    int loadLibrary(const juce::String& directoryPath, bool recursive = true, bool clearExisting = true);

    /**
     * @brief Load library from multiple directory paths
     * @param directoryPaths Array of directory paths to scan
     * @param recursive Whether to scan subdirectories
     * @param clearExisting If true, clears existing data before loading
     * @return Number of files successfully loaded
     */
    int loadLibrary(const juce::StringArray& directoryPaths, bool recursive = true, bool clearExisting = true);

    /**
     * @brief Clear all library data
     */
    void clearLibrary();

    /**
     * @brief Get the library ValueTree (read-only)
     */
    const juce::ValueTree& getLibraryTree() const
    {
        return libraryTree;
    }

    void setItemSelectedCallback(ItemSelectedCallback callback);
    void updateItemList();
    void setLabelText(const juce::String& text);
    void setPlaceholderText(const juce::String& text);

    /**
     * @brief Show or hide the Browse button
     * @param visible True to show, false to hide
     */
    void setBrowseButtonVisible(bool visible);

    /**
     * @brief Show or hide the WASD navigation button
     * @param visible True to show, false to hide
     */
    void setWasdButtonVisible(bool visible);

    /**
     * @brief Show or hide the text label
     * @param visible True to show, false to hide
     */
    void setLabelVisible(bool visible);

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

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

    // Custom non-modal filtered list popup
    class FilteredListPopup : public juce::Component
    {
    public:
        struct Item
        {
            juce::String category; // Parent node's name property (category/group identifier)
            juce::String label;    // Child node's name property (item display name)
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

    // ValueTree node index structure for hierarchical navigation
    struct NodeIndex
    {
        int fileIdx;     // Index of file node in library
        int categoryIdx; // Index of category node in file
        int childIdx;    // Index of child node in category
    };

    std::vector<NodeIndex> itemIndices;
    std::vector<NodeIndex> flatItemList; // Flat list of all items for WASD navigation

    // ValueTree management
    juce::ValueTree parentState;
    juce::ValueTree libraryTree;
    std::unique_ptr<PresetConverter> converter;

    ItemSelectedCallback itemSelectedCallback;

    juce::Label label;
    SearchTextEditor textEditor;
    juce::TextButton dropdownButton;
    juce::TextButton wasdToggleButton;
    std::unique_ptr<FilteredListPopup> filteredPopup;

    juce::String currentItemName;
    int currentFlatIndex = -1; // Current position in flatItemList for WASD navigation

    // Visibility preferences
    bool browseButtonVisible = true;
    bool wasdButtonVisible = true;
    bool labelVisible = true;

    void buildFlatItemList();
    void navigateToFlatIndex(int index);
    void applyCurrentItem();
    void updateWasdToggleState(bool enabled);

    // Helper methods for library management
    juce::Array<juce::File> scanFiles(const juce::String& directoryPath, bool recursive);
    int loadFilesIntoTree(const juce::Array<juce::File>& files);

    static LibraryBrowser* activeWasdInstance; // Track which instance has WASD enabled

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryBrowser)
};
