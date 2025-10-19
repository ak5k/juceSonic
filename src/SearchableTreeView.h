#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

/**
 * @brief Base class for tree items that support search/match highlighting
 */
class SearchableTreeItem : public juce::TreeViewItem
{
public:
    virtual ~SearchableTreeItem() = default;

    // Abstract methods that derived classes must implement
    virtual juce::String getName() const = 0;
    virtual bool matchesSearch(const juce::String& searchTerm) const;

    // Match state management
    void setMatched(bool matched)
    {
        isMatched = matched;
    }

    bool getMatched() const
    {
        return isMatched;
    }

    // Focus state management (for Ctrl navigation cursor)
    void setFocused(bool focused)
    {
        isFocused = focused;
    }

    bool getFocused() const
    {
        return isFocused;
    }

    // Hidden state for filtering (items that don't match search)
    void setHidden(bool hidden)
    {
        isHidden = hidden;
    }

    bool getHidden() const
    {
        return isHidden;
    }

    // Override to customize match highlighting
    virtual void paintMatchHighlight(juce::Graphics& g, int width, int height);

    // Default paintItem implementation - draws highlight and text
    // Derived classes can override for custom rendering
    void paintItem(juce::Graphics& g, int width, int height) override;

    // Override getItemHeight to hide filtered items
    int getItemHeight() const override
    {
        if (isHidden)
            return 0;
        return juce::TreeViewItem::getItemHeight();
    }

    // Check if this item should be auto-expanded (has metadata children)
    virtual bool shouldAutoExpand() const
    {
        return false;
    }

    // Override to notify tree view when item is opened/closed
    void itemOpennessChanged(bool isNowOpen) override;

protected:
    bool isMatched = false;
    bool isFocused = false;
    bool isHidden = false;
};

/**
 * @brief Custom TextEditor that forwards down arrow key to parent
 */
class SearchTextEditor : public juce::TextEditor
{
public:
    SearchTextEditor() = default;

    void setSearchableTreeView(class SearchableTreeView* view)
    {
        treeView = view;
    }

    bool keyPressed(const juce::KeyPress& key) override;

private:
    SearchableTreeView* treeView = nullptr;
};

/**
 * @brief Custom TreeView that handles filtered navigation
 */
class FilteredTreeView : public juce::TreeView
{
public:
    FilteredTreeView() = default;

    void setSearchableTreeView(SearchableTreeView* view)
    {
        searchView = view;
    }

    void setFiltered(bool filtered)
    {
        isFiltered = filtered;
    }

    bool isCurrentlyFiltered() const
    {
        return isFiltered;
    }

    bool keyPressed(const juce::KeyPress& key) override;

    void mouseDown(const juce::MouseEvent& e) override;

    bool hitTest(int x, int y) override;

    // Set focus highlight on an item (for Ctrl navigation)
    void setFocusedItem(juce::TreeViewItem* item);

private:
    SearchableTreeView* searchView = nullptr;
    bool isFiltered = false;
    juce::TreeViewItem* focusedItem = nullptr;        // Visual focus indicator (Ctrl navigation)
    juce::TreeViewItem* lastNavigationItem = nullptr; // Internal reference for continuing navigation

    void collectMatchedItems(juce::Array<juce::TreeViewItem*>& items, juce::TreeViewItem* item);
    void collectVisibleSelectableItems(juce::Array<juce::TreeViewItem*>& items, juce::TreeViewItem* item);
    void clearAllFocusedStates(juce::TreeViewItem* item);
};

/**
 * @brief Reusable component providing a tree view with live search functionality
 *
 * Features:
 * - Live search with configurable minimum character threshold
 * - Keyboard navigation between matches
 * - Visual distinction between matched items and current selection
 * - Automatic expansion of matched item hierarchies
 *
 * Usage:
 * - Inherit from this class
 * - Override createRootItem() to populate the tree
 * - Override onSelectionChanged() to handle selection events
 * - Optionally override getMinSearchLength() to change search threshold
 */
class SearchableTreeView
    : public juce::Component
    , private juce::TextEditor::Listener
{
    friend class FilteredTreeView; // Allow FilteredTreeView to call onEnterKeyPressed

public:
    SearchableTreeView();
    ~SearchableTreeView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // TextEditor::Listener
    void textEditorTextChanged(juce::TextEditor& editor) override;
    void textEditorReturnKeyPressed(juce::TextEditor& editor) override;

    // Command callback - called when user triggers default action (e.g., Enter key in tree)
    // Receives array of selected items
    std::function<void(const juce::Array<juce::TreeViewItem*>&)> onCommand;

    // Search results callback - called after filtering completes
    // Receives: (searchTerm, matchCount)
    std::function<void(const juce::String&, int)> onSearchResultsChanged;

    // Control whether Enter in search field triggers command (default: false)
    bool triggerCommandFromSearchField = false;

    // Focus management (called by custom widgets)
    void moveFocusToTree();
    void moveFocusToSearchField();
    void insertTextIntoSearchField(const juce::String& text);

    // Tree access
    FilteredTreeView& getTreeView()
    {
        return treeView;
    }

    juce::TreeViewItem* getRootItem()
    {
        return rootItem.get();
    }

    // Selection helpers
    juce::Array<juce::TreeViewItem*> getSelectedItems();

    // Virtual methods for derived classes to override

    /**
     * @brief Create and populate the root tree item
     * Called when the tree needs to be (re)built
     */
    virtual std::unique_ptr<juce::TreeViewItem> createRootItem() = 0;

    /**
     * @brief Called when selection changes
     * Override to update UI based on selection
     */
    virtual void onSelectionChanged()
    {
    }

    /**
     * @brief Called when Enter is pressed with items selected
     * Override to provide default action (or use onCommand callback instead)
     */
    virtual void onEnterKeyPressed(juce::TreeViewItem* selectedItem)
    {
    }

    /**
     * @brief Called when an item is selected from the browse menu
     * Override to perform action on the selected item (e.g., load preset)
     * Default implementation does nothing - the item is already selected
     *
     * @param selectedItem The item that was chosen from the browse menu
     */
    virtual void onBrowseMenuItemSelected(juce::TreeViewItem* selectedItem)
    {
    }

    /**
     * @brief Get minimum search term length before filtering activates
     * Default is 3 characters
     */
    virtual int getMinSearchLength() const
    {
        return 3;
    }

    /**
     * @brief Get placeholder text for search field
     */
    virtual juce::String getSearchPlaceholder() const
    {
        return "Type to search...";
    }

    /**
     * @brief Get metadata text to display below the tree
     * Override to show status/info text (e.g., "50 items from 3 repositories")
     */
    virtual juce::String getMetadataText() const
    {
        return {};
    }

    /**
     * @brief Get metadata sub-items for a tree item
     * Override to provide metadata children for items (author, version, description, etc.)
     * Return empty array if no metadata should be shown
     *
     * @param item The item to get metadata for
     * @return Array of name-value pairs to display as sub-items
     */
    virtual juce::Array<std::pair<juce::String, juce::String>> getMetadataForItem(juce::TreeViewItem* item)
    {
        return {};
    }

    /**
     * @brief Check if an item should be included in search matching
     * Override to exclude certain items (e.g., metadata) from search results
     *
     * @param item The item to check
     * @return true if the item should be searchable
     */
    virtual bool shouldIncludeInSearch(juce::TreeViewItem* item)
    {
        return true; // By default, include all items in search
    }

    /**
     * @brief Check if an item should be counted for the total count display
     * Override to customize counting logic (e.g., count only presets, not directories/banks)
     *
     * @param item The item to check
     * @return true if the item should be counted
     */
    virtual bool shouldCountItem(juce::TreeViewItem* item)
    {
        // By default, count leaf items only
        return item && item->getNumSubItems() == 0;
    }

    /**
     * @brief Refresh/rebuild the entire tree
     */
    void refreshTree();

    /**
     * @brief Trigger filtering with current search term
     */
    void filterTree();

    /**
     * @brief Execute command on selected items
     * Handles setting search text (if enabled) and calling command callback or virtual method
     * @param selectedItems The items to execute the command on
     */
    void executeCommand(const juce::Array<juce::TreeViewItem*>& selectedItems);

    /**
     * @brief Clear all matched flags in the tree
     */
    void clearMatches(juce::TreeViewItem* item = nullptr);

    /**
     * @brief Recursively mark items matching the search term
     * Returns true if this item or any descendants match
     */
    bool markMatches(juce::TreeViewItem* item, const juce::String& searchTerm);

    /**
     * @brief Control visibility of search field
     */
    void setShowSearchField(bool show);

    /**
     * @brief Control visibility of metadata label
     */
    void setShowMetadataLabel(bool show);

    /**
     * @brief Called when tree item openness changes (for dynamic height adjustment)
     */
    void onTreeItemOpennessChanged();

    /**
     * @brief Control tree visibility directly
     */
    void setTreeVisible(bool visible);

    /**
     * @brief Enable auto-hiding tree when no search results
     * When true, tree is only visible when search produces matches
     */
    void setAutoHideTreeWithoutResults(bool autoHide);

    /**
     * @brief Check if auto-hide mode is enabled
     */
    bool isAutoHideEnabled() const
    {
        return autoHideTreeWithoutResults;
    }

    /**
     * @brief Get the needed height for the component based on current state
     */
    int getNeededHeight() const;

    /**
     * @brief Get ideal tree height based on visible items
     */
    int getIdealTreeHeight() const;

    /**
     * @brief Check if component is in collapsed preview mode
     */
    bool isInCollapsedMode() const;

    /**
     * @brief Toggle manual expansion state (for use by FilteredTreeView)
     */
    void toggleManualExpansion();

    /**
     * @brief Handle ESC key press - collapse tree and clear search
     */
    void handleEscapeKey();

    /**
     * @brief Mouse event handling for click-to-expand in collapsed mode and click-away detection
     */
    void mouseDown(const juce::MouseEvent& e) override;

    /**
     * @brief Handle parent hierarchy changes to set up global mouse listener
     */
    void parentHierarchyChanged() override;

    /**
     * @brief Custom hit testing for collapsed mode
     */
    bool hitTest(int x, int y) override;

    /**
     * @brief Callback when tree expansion state changes (for parent layout updates)
     */
    std::function<void(bool isExpanded)> onTreeExpansionChanged;

    /**
     * @brief Get current visibility state of search field
     */
    bool isSearchFieldVisible() const
    {
        return showSearchField;
    }

    /**
     * @brief Get current visibility state of metadata label
     */
    bool isMetadataLabelVisible() const
    {
        return showMetadataLabel;
    }

    /**
     * @brief Check if there are any matched items in the current search
     */
    bool hasMatches() const;

    // Current search term
    juce::String currentSearchTerm;

protected:
    /**
     * @brief Get the deepest level items in the tree hierarchy
     * Override in derived classes to define what constitutes the "deepest" items
     * (e.g., for presets: Preset items; for repositories: Package items)
     */
    virtual juce::Array<juce::TreeViewItem*> getDeepestLevelItems();

    /**
     * @brief Get the parent category name for a deepest level item
     * Override to define how items are grouped in the browse menu
     * (e.g., for presets: return bank/file name; for repositories: return category)
     */
    virtual juce::String getParentCategoryForItem(juce::TreeViewItem* item);

private:
    // UI Components
    juce::Label searchLabel;
    SearchTextEditor searchField;
    juce::TextButton browseButton;
    FilteredTreeView treeView;
    juce::Label metadataLabel;
    std::unique_ptr<juce::TreeViewItem> rootItem;

    // UI visibility flags
    bool showSearchField = true;
    bool showMetadataLabel = true;
    bool autoHideTreeWithoutResults = false;
    bool isTreeManuallyExpanded = false; // User clicked to expand/collapse
    int matchCount = 0;

    // Browse menu helpers
    void showBrowseMenu();
    void buildBrowseMenu(
        juce::PopupMenu& menu,
        const juce::StringArray& categories,
        const juce::HashMap<juce::String, juce::Array<juce::TreeViewItem*>>& itemsByCategory,
        int& itemId,
        int categoryIndex,
        juce::HashMap<int, juce::TreeViewItem*>& itemIdToTreeItem
    );

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SearchableTreeView)
};
