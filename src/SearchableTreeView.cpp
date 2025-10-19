#include "SearchableTreeView.h"

// ============================================================================
// SearchableTreeItem implementation
// ============================================================================

bool SearchableTreeItem::matchesSearch(const juce::String& searchTerm) const
{
    return getName().containsIgnoreCase(searchTerm);
}

void SearchableTreeItem::itemOpennessChanged(bool isNowOpen)
{
    // Call base class implementation first
    juce::TreeViewItem::itemOpennessChanged(isNowOpen);

    // Notify parent SearchableTreeView that tree structure changed
    if (auto* treeView = getOwnerView())
    {
        if (auto* searchableTree = dynamic_cast<SearchableTreeView*>(treeView->getParentComponent()))
            searchableTree->onTreeItemOpennessChanged();
    }
}

void SearchableTreeItem::paintMatchHighlight(juce::Graphics& g, int width, int height)
{
    // Get colors from LookAndFeel
    auto* ownerView = getOwnerView();
    if (!ownerView)
        return;

    auto& lf = ownerView->getLookAndFeel();

    // Draw background based on state
    if (isSelected())
    {
        // Selected - use default highlight color from LookAndFeel
        auto highlightColour = lf.findColour(juce::TextEditor::highlightColourId);
        g.fillAll(highlightColour);

        // If also focused (Ctrl navigation cursor on selected item), draw border
        if (isFocused)
        {
            auto focusColour = lf.findColour(juce::TextEditor::focusedOutlineColourId);
            g.setColour(focusColour);
            g.drawRect(0, 0, width, height, 2);
        }
    }
    else if (isFocused)
    {
        // Focused but not selected (Ctrl navigation cursor) - use focused outline color as secondary
        // This gives a distinct color from selection
        auto focusColour = lf.findColour(juce::TextEditor::focusedOutlineColourId);
        g.fillAll(focusColour.withAlpha(0.4f));
    }
    else if (isMatched)
    {
        // Matched but not selected - subtle highlight
        auto highlightColour = lf.findColour(juce::TextEditor::highlightColourId);
        g.fillAll(highlightColour.withAlpha(0.15f));
    }
}

// ============================================================================
// SearchTextEditor implementation
// ============================================================================

bool SearchTextEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::downKey && treeView)
    {
        treeView->moveFocusToTree();
        return true;
    }

    // Let TextEditor handle all other keys (including up arrow for cursor movement)
    return juce::TextEditor::keyPressed(key);
}

// ============================================================================
// FilteredTreeView implementation
// ============================================================================

void FilteredTreeView::collectMatchedItems(juce::Array<juce::TreeViewItem*>& items, juce::TreeViewItem* item)
{
    if (!item)
        return;

    // Collect matched items that can be selected
    if (auto* searchableItem = dynamic_cast<SearchableTreeItem*>(item))
    {
        if (searchableItem->getMatched() && item->canBeSelected())
            items.add(item);
    }

    for (int i = 0; i < item->getNumSubItems(); ++i)
        collectMatchedItems(items, item->getSubItem(i));
}

void FilteredTreeView::collectVisibleSelectableItems(juce::Array<juce::TreeViewItem*>& items, juce::TreeViewItem* item)
{
    if (!item)
        return;

    // Only add items that can be selected
    if (item->canBeSelected())
        items.add(item);

    // Recurse into open items
    if (item->isOpen())
        for (int i = 0; i < item->getNumSubItems(); ++i)
            collectVisibleSelectableItems(items, item->getSubItem(i));
}

void FilteredTreeView::clearAllFocusedStates(juce::TreeViewItem* item)
{
    if (!item)
        return;

    if (auto* searchableItem = dynamic_cast<SearchableTreeItem*>(item))
        searchableItem->setFocused(false);

    for (int i = 0; i < item->getNumSubItems(); ++i)
        clearAllFocusedStates(item->getSubItem(i));
}

void FilteredTreeView::setFocusedItem(juce::TreeViewItem* item)
{
    // Clear all focused states first
    if (auto* root = getRootItem())
        for (int i = 0; i < root->getNumSubItems(); ++i)
            clearAllFocusedStates(root->getSubItem(i));

    // Set the new focused item
    if (item)
    {
        if (auto* searchableItem = dynamic_cast<SearchableTreeItem*>(item))
        {
            searchableItem->setFocused(true);
            focusedItem = item;
            item->repaintItem();
        }
    }
    else
    {
        focusedItem = nullptr;
    }
}

bool FilteredTreeView::keyPressed(const juce::KeyPress& key)
{
    auto keyCode = key.getKeyCode();

    // Helper lambda to collect items based on filter state
    auto collectItems = [this](juce::Array<juce::TreeViewItem*>& items)
    {
        auto* root = getRootItem();
        if (!root)
            return;

        if (isFiltered && searchView)
        {
            for (int i = 0; i < root->getNumSubItems(); ++i)
                if (auto* item = root->getSubItem(i))
                    collectMatchedItems(items, item);
        }
        else
        {
            for (int i = 0; i < root->getNumSubItems(); ++i)
                collectVisibleSelectableItems(items, root->getSubItem(i));
        }
    };

    // Helper lambda to find first selected item
    auto findFirstSelected = [this]() -> juce::TreeViewItem*
    {
        for (int i = 0; i < getNumSelectedItems(); ++i)
        {
            auto* item = getSelectedItem(i);
            if (item && item->canBeSelected())
                return item;
        }
        return nullptr;
    };

    // Check if this is a navigation key (up/down/j/k)
    bool isNavigationKey =
        (keyCode == juce::KeyPress::upKey
         || keyCode == juce::KeyPress::downKey
         || keyCode == 'j'
         || keyCode == 'J'
         || keyCode == 'k'
         || keyCode == 'K');

    // Show immediate focus indicator when Ctrl+navigation key pressed (without Shift)
    auto modifiers = juce::ModifierKeys::currentModifiers;
    if (isNavigationKey && modifiers.isCtrlDown() && !modifiers.isShiftDown() && !focusedItem)
    {
        if (auto* firstSelected = findFirstSelected())
        {
            juce::Array<juce::TreeViewItem*> items;
            collectItems(items);
            int idx = items.indexOf(firstSelected);
            if (idx >= 0)
                setFocusedItem(items[idx]);
        }
    }

    // Handle Enter key for command execution
    if (key == juce::KeyPress::returnKey && searchView)
    {
        // Get all selected items (filter out non-selectable items like metadata)
        juce::Array<juce::TreeViewItem*> selectedItems;
        for (int i = 0; i < getNumSelectedItems(); ++i)
        {
            auto* item = getSelectedItem(i);
            if (item && item->canBeSelected())
                selectedItems.add(item);
        }

        if (!selectedItems.isEmpty())
        {
            // Call the command callback if set, otherwise call virtual method
            if (searchView->onCommand)
            {
                searchView->onCommand(selectedItems);
            }
            else if (selectedItems.size() == 1)
            {
                // Fallback to virtual method for single item (backward compatibility)
                searchView->onEnterKeyPressed(selectedItems[0]);
            }

            return true;
        }
    }

    // Handle Ctrl+Space for toggling selection of current item
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        modifiers = juce::ModifierKeys::currentModifiers;
        if (modifiers.isCtrlDown())
        {
            // Toggle the focused item if it exists, otherwise use first selected item
            juce::TreeViewItem* currentItem = focusedItem ? focusedItem : findFirstSelected();

            if (currentItem && currentItem->canBeSelected())
            {
                currentItem->setSelected(!currentItem->isSelected(), false);
                return true;
            }
        }
    }

    // Handle up/down navigation (arrow keys or vim keys: j/k)
    if (isNavigationKey)
    {
        // Get current modifier state
        bool shiftHeld = juce::ModifierKeys::currentModifiers.isShiftDown();
        bool ctrlHeld = juce::ModifierKeys::currentModifiers.isCtrlDown();
        bool isDown = (keyCode == juce::KeyPress::downKey || keyCode == 'j' || keyCode == 'J');

        // Collect appropriate item list
        juce::Array<juce::TreeViewItem*> items;
        collectItems(items);

        if (items.isEmpty())
            return juce::TreeView::keyPressed(key);

        // Find currently selected/focused item (use appropriate edge based on direction and modifiers)
        int currentIndex = -1;

        if (ctrlHeld)
        {
            // Ctrl navigation: use lastNavigationItem if exists, otherwise first selected
            if (lastNavigationItem)
                currentIndex = items.indexOf(lastNavigationItem);
            else if (auto* firstSelected = findFirstSelected())
                currentIndex = items.indexOf(firstSelected);
        }
        else if (shiftHeld && !ctrlHeld)
        {
            // For Shift navigation: extend from the edge of the selection range we're currently inside
            // If we're not inside any range (lastNavigationItem is unselected), start new range from there

            if (lastNavigationItem)
            {
                int navIndex = items.indexOf(lastNavigationItem);

                // Check if lastNavigationItem is selected (we're inside a range)
                bool isSelected = false;
                for (int i = 0; i < getNumSelectedItems(); ++i)
                {
                    if (getSelectedItem(i) == lastNavigationItem)
                    {
                        isSelected = true;
                        break;
                    }
                }

                if (isSelected)
                {
                    // We're inside a selection range - find the edge of THIS range (not global edge)
                    // A range is a contiguous block of selected items
                    if (isDown)
                    {
                        // Find the bottom edge of the range containing navIndex
                        currentIndex = navIndex;
                        for (int i = navIndex + 1; i < items.size(); ++i)
                        {
                            bool itemSelected = false;
                            for (int j = 0; j < getNumSelectedItems(); ++j)
                            {
                                if (getSelectedItem(j) == items[i] && items[i]->canBeSelected())
                                {
                                    itemSelected = true;
                                    currentIndex = i;
                                    break;
                                }
                            }
                            if (!itemSelected)
                                break; // Hit end of contiguous range
                        }
                    }
                    else // up
                    {
                        // Find the top edge of the range containing navIndex
                        currentIndex = navIndex;
                        for (int i = navIndex - 1; i >= 0; --i)
                        {
                            bool itemSelected = false;
                            for (int j = 0; j < getNumSelectedItems(); ++j)
                            {
                                if (getSelectedItem(j) == items[i] && items[i]->canBeSelected())
                                {
                                    itemSelected = true;
                                    currentIndex = i;
                                    break;
                                }
                            }
                            if (!itemSelected)
                                break; // Hit start of contiguous range
                        }
                    }
                }
                else
                {
                    // lastNavigationItem is outside selection - start new disconnected range
                    // Select the starting point first
                    lastNavigationItem->setSelected(true, false);
                    currentIndex = navIndex;
                }
            }
            else
            {
                // No lastNavigationItem, fall back to global edge-finding
                if (isDown)
                {
                    // Going down: find the highest index (bottom edge of all selections)
                    for (int i = 0; i < getNumSelectedItems(); ++i)
                    {
                        auto* selected = getSelectedItem(i);
                        if (selected && selected->canBeSelected())
                        {
                            int idx = items.indexOf(selected);
                            if (idx > currentIndex)
                                currentIndex = idx;
                        }
                    }
                }
                else
                {
                    // Going up: find the lowest index (top edge of all selections)
                    currentIndex = items.size(); // Start high
                    for (int i = 0; i < getNumSelectedItems(); ++i)
                    {
                        auto* selected = getSelectedItem(i);
                        if (selected && selected->canBeSelected())
                        {
                            int idx = items.indexOf(selected);
                            if (idx >= 0 && idx < currentIndex)
                                currentIndex = idx;
                        }
                    }
                    if (currentIndex == items.size())
                        currentIndex = -1; // Reset if nothing found
                }
            }
        }
        else
        {
            // Normal navigation: use lastNavigationItem if exists, otherwise first selected
            if (lastNavigationItem)
                currentIndex = items.indexOf(lastNavigationItem);
            if (currentIndex < 0)
                if (auto* firstSelected = findFirstSelected())
                    currentIndex = items.indexOf(firstSelected);
        } // If Ctrl is held and we have a current item, show focus immediately
        if (ctrlHeld && !shiftHeld && currentIndex >= 0 && currentIndex < items.size())
            setFocusedItem(items[currentIndex]);

        // Calculate new index
        int newIndex = currentIndex;
        if (isDown)
        {
            if (currentIndex < 0)
                newIndex = 0; // Select first if none selected
            else if (currentIndex < items.size() - 1)
                newIndex = currentIndex + 1;
            else if (isFiltered)
                newIndex = (currentIndex + 1) % items.size(); // Wrap around in filtered mode
        }
        else // up key
        {
            // In filtered mode, move focus back to search field at first item
            if (isFiltered && currentIndex <= 0 && searchView)
            {
                searchView->moveFocusToSearchField();
                return true;
            }

            if (currentIndex < 0)
                newIndex = items.size() - 1; // Select last if none selected
            else if (currentIndex > 0)
                newIndex = currentIndex - 1;
        }

        // Apply selection based on modifiers
        if (newIndex >= 0 && newIndex < items.size())
        {
            auto* newItem = items[newIndex];
            bool indexChanged = (newIndex != currentIndex);

            if (shiftHeld && !ctrlHeld)
            {
                // Shift only: Add to selection (preserve previous selections)
                if (indexChanged)
                    newItem->setSelected(true, false);
                // Update reference point for next navigation, but no visual focus indicator
                lastNavigationItem = newItem;
                setFocusedItem(nullptr);
            }
            else if (ctrlHeld && shiftHeld)
            {
                // Ctrl+Shift: Move focus AND extend selection from current position to new position
                if (indexChanged)
                {
                    // Select all items in the range from currentIndex to newIndex
                    int start = juce::jmin(currentIndex, newIndex);
                    int end = juce::jmax(currentIndex, newIndex);
                    for (int i = start; i <= end; ++i)
                        if (i >= 0 && i < items.size() && items[i]->canBeSelected())
                            items[i]->setSelected(true, false);
                }
                // Update reference point and show focus indicator
                lastNavigationItem = newItem;
                setFocusedItem(newItem);
            }
            else if (ctrlHeld && !shiftHeld)
            {
                // Ctrl only: Move without selecting (preserve all selections)
                // Set focus indicator to show current position AND update reference point
                lastNavigationItem = newItem;
                setFocusedItem(newItem);
            }
            else
            {
                // No modifiers: Normal navigation - clear previous, select new
                if (indexChanged)
                {
                    clearSelectedItems();
                    newItem->setSelected(true, true);
                }
                // Clear both reference point and visual focus
                lastNavigationItem = nullptr;
                setFocusedItem(nullptr);
            }

            scrollToKeepItemVisible(newItem);
        }

        return true;
    }

    // Handle left/right navigation (arrow keys or vim keys: h/l) for expanding/collapsing
    bool isLeftRight =
        (keyCode == juce::KeyPress::leftKey
         || keyCode == juce::KeyPress::rightKey
         || keyCode == 'h'
         || keyCode == 'H'
         || keyCode == 'l'
         || keyCode == 'L');

    if (isLeftRight)
    {
        bool isRight = (keyCode == juce::KeyPress::rightKey || keyCode == 'l' || keyCode == 'L');

        // Prioritize lastNavigationItem, fall back to first selected item
        juce::TreeViewItem* targetItem = lastNavigationItem ? lastNavigationItem : findFirstSelected();

        if (targetItem)
        {
            if (isRight && targetItem->mightContainSubItems() && !targetItem->isOpen())
                targetItem->setOpen(true);
            else if (!isRight && targetItem->isOpen())
                targetItem->setOpen(false);

            return true; // Prevent default JUCE behavior
        }
    }

    return juce::TreeView::keyPressed(key);
}

void FilteredTreeView::mouseDown(const juce::MouseEvent& e)
{
    // Check if we're in collapsed hint line mode
    if (searchView && searchView->isAutoHideEnabled() && searchView->isInCollapsedMode())
    {
        // In collapsed mode, any click on the tree view should expand it
        // Trigger expansion by toggling manual expansion state
        searchView->toggleManualExpansion();
        return; // Don't pass to base class - we handled it
    }

    // Normal mode - let TreeView handle the click
    juce::TreeView::mouseDown(e);
}

bool FilteredTreeView::hitTest(int x, int y)
{
    // In collapsed hint line mode, accept all hits so we can handle the click
    if (searchView && searchView->isAutoHideEnabled() && searchView->isInCollapsedMode())

        return true; // Accept all mouse events in collapsed mode

    // Normal mode - use default TreeView hit testing
    bool result = juce::TreeView::hitTest(x, y);

    return result;
}

// ============================================================================
// SearchableTreeView implementation
// ============================================================================

SearchableTreeView::SearchableTreeView()
{
    // Setup search field
    addAndMakeVisible(searchLabel);
    searchLabel.setText("Search:", juce::dontSendNotification);
    searchLabel.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(searchField);
    searchField.setTextToShowWhenEmpty(getSearchPlaceholder(), juce::Colours::grey);
    searchField.addListener(this);
    searchField.setWantsKeyboardFocus(true);
    searchField.setSearchableTreeView(this);

    // Setup tree view
    addAndMakeVisible(treeView);
    treeView.setMultiSelectEnabled(true);
    treeView.setRootItemVisible(false);
    treeView.setWantsKeyboardFocus(true);
    treeView.setSearchableTreeView(this);
    treeView.setInterceptsMouseClicks(true, true); // Ensure tree receives all mouse events
    treeView.addMouseListener(this, true); // Add mouse listener to catch all mouse events including from children

    // Setup metadata label
    addAndMakeVisible(metadataLabel);
    metadataLabel.setJustificationType(juce::Justification::centred);
    metadataLabel.setColour(juce::Label::textColourId, juce::Colours::grey);

    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true); // Ensure we receive mouse events for click-away detection
}

SearchableTreeView::~SearchableTreeView()
{
    treeView.setRootItem(nullptr);
}

void SearchableTreeView::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void SearchableTreeView::resized()
{
    auto bounds = getLocalBounds();

    // Search bar at top (if visible)
    if (showSearchField)
    {
        auto searchArea = bounds.removeFromTop(25); // Half the previous height (50 -> 25)
        searchLabel.setBounds(searchArea.removeFromLeft(60));
        searchField.setBounds(searchArea);

        // Add some spacing between search and tree
        bounds.removeFromTop(5); // Half the previous spacing (10 -> 5)
    }

    // Metadata label at bottom (if visible)
    if (showMetadataLabel)
    {
        auto metadataArea = bounds.removeFromBottom(20);
        metadataLabel.setBounds(metadataArea);
    }

    // Handle auto-hide mode with collapsed preview
    if (autoHideTreeWithoutResults)
    {
        bool hasActiveSearch = currentSearchTerm.length() >= getMinSearchLength();
        bool hasResults = matchCount > 0;
        bool shouldExpand = (hasActiveSearch && hasResults) || isTreeManuallyExpanded;

        if (shouldExpand)
        {
            // Expanded: use calculated ideal height
            int idealHeight = getIdealTreeHeight();
            auto treeArea = bounds.withHeight(idealHeight);
            treeView.setBounds(treeArea);
            treeView.setVisible(true);
            treeView.setInterceptsMouseClicks(true, true); // Allow tree to handle clicks normally
        }
        else
        {
            // Collapsed: show just the first line of the tree (one item height ~24-26px)
            int itemHeight = 24; // Standard tree item height
            auto treeArea = bounds.withHeight(itemHeight);
            treeView.setBounds(treeArea);
            treeView.setVisible(true);
            treeView.setInterceptsMouseClicks(true, true); // Allow clicking to expand
        }
    }
    else
    {
        // Normal mode: use all remaining space
        treeView.setBounds(bounds);
        treeView.setVisible(true);
        treeView.setInterceptsMouseClicks(true, true);
    }
}

void SearchableTreeView::textEditorTextChanged(juce::TextEditor& editor)
{
    if (&editor == &searchField)
    {
        currentSearchTerm = searchField.getText().trim();
        filterTree();
    }
}

void SearchableTreeView::textEditorReturnKeyPressed(juce::TextEditor& editor)
{
    if (&editor == &searchField && currentSearchTerm.length() >= getMinSearchLength())
    {
        // Get all selected items
        auto selectedItems = getSelectedItems();

        // Only call command callback if enabled for search field
        if (triggerCommandFromSearchField && onCommand)
            onCommand(selectedItems);

        // Call the virtual method for single item (backward compatibility)
        // Only if command wasn't already triggered
        if (!triggerCommandFromSearchField && selectedItems.size() == 1)
            onEnterKeyPressed(selectedItems[0]);
    }
}

void SearchableTreeView::moveFocusToTree()
{
    // Move focus from search field to tree
    treeView.grabKeyboardFocus();

    // If there are selected items, scroll to show the first one
    if (treeView.getNumSelectedItems() > 0)
    {
        if (auto* firstSelected = treeView.getSelectedItem(0))
        {
            firstSelected->setOpenness(juce::TreeViewItem::Openness::opennessOpen);
            treeView.scrollToKeepItemVisible(firstSelected);
        }
    }
}

void SearchableTreeView::moveFocusToSearchField()
{
    searchField.grabKeyboardFocus();
}

juce::Array<juce::TreeViewItem*> SearchableTreeView::getSelectedItems()
{
    juce::Array<juce::TreeViewItem*> items;
    for (int i = 0; i < treeView.getNumSelectedItems(); ++i)
        items.add(treeView.getSelectedItem(i));
    return items;
}

void SearchableTreeView::refreshTree()
{
    treeView.setRootItem(nullptr);
    rootItem = createRootItem();
    treeView.setRootItem(rootItem.get());

    // Clear search when refreshing
    currentSearchTerm.clear();
    searchField.clear();
    treeView.setFiltered(false);

    // Update metadata label
    metadataLabel.setText(getMetadataText(), juce::dontSendNotification);
}

void SearchableTreeView::clearMatches(juce::TreeViewItem* item)
{
    if (!item)
        item = rootItem.get();

    if (!item)
        return;

    if (auto* searchableItem = dynamic_cast<SearchableTreeItem*>(item))
    {
        searchableItem->setMatched(false);
        searchableItem->setHidden(false); // Show all items when clearing filter
    }

    for (int i = 0; i < item->getNumSubItems(); ++i)
        clearMatches(item->getSubItem(i));
}

bool SearchableTreeView::markMatches(juce::TreeViewItem* item, const juce::String& searchTerm)
{
    if (!item)
        return false;

    auto* searchableItem = dynamic_cast<SearchableTreeItem*>(item);
    if (!searchableItem)
        return false;

    // Check if this item should be included in search
    bool shouldSearch = shouldIncludeInSearch(item);
    bool thisMatches = shouldSearch && searchableItem->matchesSearch(searchTerm);
    bool childrenMatch = false;

    // Check children recursively
    for (int i = 0; i < item->getNumSubItems(); ++i)
        if (markMatches(item->getSubItem(i), searchTerm))
            childrenMatch = true;

    // An item should be visible if it matches OR any of its children match
    bool shouldBeVisible = thisMatches || childrenMatch;

    // Mark as matched if this item specifically matches (for highlighting)
    searchableItem->setMatched(thisMatches);

    // Hide items that don't match and have no matching children
    searchableItem->setHidden(!shouldBeVisible);

    // Expand if this item or children match (so we can see the matches)
    if (shouldBeVisible)
        item->setOpen(true);
    else
        item->setOpen(false);

    return shouldBeVisible;
}

void SearchableTreeView::filterTree()
{
    if (!rootItem)
        return;

    // If search term is less than threshold, clear filter
    if (currentSearchTerm.length() < getMinSearchLength())
    {
        treeView.setFiltered(false);

        // Clear all matches and collapse
        clearMatches();
        matchCount = 0;

        // Collapse all top-level items
        for (int i = 0; i < rootItem->getNumSubItems(); ++i)
            if (auto* item = rootItem->getSubItem(i))
                item->setOpen(false);

        treeView.clearSelectedItems();
        treeView.repaint();
        onSelectionChanged();

        // Auto-collapse when search is cleared (unless manually expanded)
        if (autoHideTreeWithoutResults)
        {
            // Reset manual expansion when search is cleared
            isTreeManuallyExpanded = false;
            resized();

            // Notify parent of collapse
            if (onTreeExpansionChanged)
                onTreeExpansionChanged(false);

            // Trigger repaint of top-level component for layout updates
            if (auto* topLevel = getTopLevelComponent())
            {
                topLevel->repaint();
                topLevel->resized();
            }
        }

        // Notify parent that search was cleared
        if (onSearchResultsChanged)
            onSearchResultsChanged(currentSearchTerm, 0);
        return;
    }

    // Set filtered state
    treeView.setFiltered(true);

    // Clear all matches first
    clearMatches();
    treeView.clearSelectedItems();
    matchCount = 0;

    // Mark matching items and count matches
    for (int i = 0; i < rootItem->getNumSubItems(); ++i)
        if (markMatches(rootItem->getSubItem(i), currentSearchTerm))
            matchCount++;

    // Auto-show/hide tree based on match results
    if (autoHideTreeWithoutResults)
    {
        bool shouldExpand = matchCount > 0;

        resized();

        // Always notify parent to recalculate height (not just when expansion state changes)
        if (onTreeExpansionChanged)
            onTreeExpansionChanged(shouldExpand);

        // Trigger repaint of top-level component for layout updates
        if (auto* topLevel = getTopLevelComponent())
        {
            topLevel->repaint();
            topLevel->resized();
        }
    }

    treeView.repaint();
    onSelectionChanged();

    // Notify parent of search results
    if (onSearchResultsChanged)
        onSearchResultsChanged(currentSearchTerm, matchCount);
}

void SearchableTreeView::setShowSearchField(bool show)
{
    if (showSearchField == show)
        return;

    showSearchField = show;
    searchLabel.setVisible(show);
    searchField.setVisible(show);
    resized();
}

void SearchableTreeView::setShowMetadataLabel(bool show)
{
    if (showMetadataLabel == show)
        return;

    showMetadataLabel = show;
    metadataLabel.setVisible(show);
    resized();
}

void SearchableTreeView::onTreeItemOpennessChanged()
{
    // Tree structure changed - update size to match tree content
    if (autoHideTreeWithoutResults)
    {
        // In auto-hide mode, always recalculate height when items open/close
        treeView.repaint();

        // Defer the callback to next message loop iteration to ensure tree has finished updating
        juce::MessageManager::callAsync(
            [safeThis = juce::Component::SafePointer<SearchableTreeView>(this)]()
            {
                if (safeThis != nullptr && safeThis->onTreeExpansionChanged)
                {
                    // Pass current expansion state
                    bool isExpanded = safeThis->isTreeManuallyExpanded
                                   || (safeThis->currentSearchTerm.length() >= safeThis->getMinSearchLength()
                                       && safeThis->matchCount > 0);
                    safeThis->onTreeExpansionChanged(isExpanded);
                }
            }
        );
    }
    else
    {
        // Even in normal mode, repaint to update display
        treeView.repaint();
    }
}

void SearchableTreeView::setTreeVisible(bool visible)
{
    if (treeView.isVisible() == visible)
        return;

    treeView.setVisible(visible);
    resized();
}

void SearchableTreeView::setAutoHideTreeWithoutResults(bool autoHide)
{
    if (autoHideTreeWithoutResults == autoHide)
        return;

    autoHideTreeWithoutResults = autoHide;

    // Let resized() handle visibility based on auto-hide logic
    resized();
}

bool SearchableTreeView::hasMatches() const
{
    return matchCount > 0;
}

int SearchableTreeView::getNeededHeight() const
{
    int height = 0; // Title is now handled by parent

    // Search field height (if visible)
    if (showSearchField)
        height += 25 + 5; // search field + spacing (reduced from 50+10 to 25+5)

    if (autoHideTreeWithoutResults)
    {
        bool hasActiveSearch = currentSearchTerm.length() >= getMinSearchLength();
        bool hasResults = matchCount > 0;
        bool shouldExpand = (hasActiveSearch && hasResults) || isTreeManuallyExpanded;

        if (shouldExpand)
        {
            // Expanded: use ideal tree height
            int idealHeight = getIdealTreeHeight();
            height += idealHeight;
        }
        else
        {
            // Collapsed: just show first tree line (one item height)
            height += 24; // One tree item height
        }
    }
    else
    {
        // Normal mode: use some reasonable default height
        height += 200;
    }

    // Metadata label height (if visible)
    if (showMetadataLabel)
        height += 20;

    return height;
}

int SearchableTreeView::getIdealTreeHeight() const
{
    if (!rootItem)
        return 200;

    // Count visible items (not filtered/hidden)
    int visibleItems = 0;
    std::function<void(juce::TreeViewItem*)> countItems = [&](juce::TreeViewItem* item)
    {
        if (!item)
            return;

        auto* searchableItem = dynamic_cast<SearchableTreeItem*>(item);
        if (!searchableItem || !searchableItem->getHidden())
            visibleItems++;

        // Count children if item is open
        if (item->isOpen())
            for (int i = 0; i < item->getNumSubItems(); ++i)
                countItems(item->getSubItem(i));
    };

    for (int i = 0; i < rootItem->getNumSubItems(); ++i)
        countItems(rootItem->getSubItem(i));

    // Calculate height: item height * count + some padding
    int itemHeight = 22;                                   // standard tree item height
    int calculatedHeight = visibleItems * itemHeight + 20; // +20 for padding

    // For auto-hide mode, return calculated height (with reasonable max)
    // Don't limit based on parent height to avoid circular dependency
    if (autoHideTreeWithoutResults)
    {
        // Just apply a reasonable maximum to prevent absurd heights
        return juce::jlimit(100, 600, calculatedHeight);
    }

    // In normal mode, return the calculated height (parent will constrain it)
    return calculatedHeight;
}

bool SearchableTreeView::isInCollapsedMode() const
{
    if (!autoHideTreeWithoutResults)
        return false;

    bool hasActiveSearch = currentSearchTerm.length() >= getMinSearchLength();
    bool hasResults = matchCount > 0;
    return !((hasActiveSearch && hasResults) || isTreeManuallyExpanded);
}

void SearchableTreeView::toggleManualExpansion()
{
    // Toggle expansion state
    isTreeManuallyExpanded = !isTreeManuallyExpanded;

    if (rootItem)
    {
        if (isTreeManuallyExpanded)
        {
            // Expanding - open all root level items and give keyboard focus

            for (int i = 0; i < rootItem->getNumSubItems(); ++i)
                if (auto* item = rootItem->getSubItem(i))

                    item->setOpen(true);

            // Give keyboard focus to tree view for navigation
            treeView.grabKeyboardFocus();
        }
        else
        {
            // Collapsing - close all root level items, deselect all, and remove focus

            for (int i = 0; i < rootItem->getNumSubItems(); ++i)
                if (auto* item = rootItem->getSubItem(i))
                    item->setOpen(false);

            // Deselect all items
            treeView.clearSelectedItems();

            // Remove keyboard focus from tree view
            if (treeView.hasKeyboardFocus(true))
                treeView.giveAwayKeyboardFocus();
        }
    }

    // Update layout
    resized();

    // Notify parent of expansion change
    if (onTreeExpansionChanged)
        onTreeExpansionChanged(isTreeManuallyExpanded);

    // Trigger repaint and ensure tree is repainted
    treeView.repaint();
    repaint();

    // Trigger repaint of top-level component for layout updates
    if (auto* topLevel = getTopLevelComponent())
    {
        topLevel->repaint();
        topLevel->resized();
    }
}

void SearchableTreeView::mouseDown(const juce::MouseEvent& e)
{
    DBG("SearchableTreeView::mouseDown at ("
        << e.x
        << ", "
        << e.y
        << "), eventComponent: "
        << e.eventComponent->getName());

    // Check if this is a click on the tree view in auto-hide mode
    if (autoHideTreeWithoutResults)
    {
        // Check for click-away when tree is manually expanded
        if (isTreeManuallyExpanded && !isInCollapsedMode())
        {
            // Get the event relative to this component
            auto localEvent = e.getEventRelativeTo(this);

            // If click is outside our bounds, collapse the tree
            if (!getLocalBounds().contains(localEvent.getPosition()))
            {
                // Collapse root level items
                if (rootItem)
                {
                    for (int i = 0; i < rootItem->getNumSubItems(); ++i)
                        if (auto* item = rootItem->getSubItem(i))
                            item->setOpen(false);
                }

                isTreeManuallyExpanded = false;

                // Update layout
                resized();

                // Notify parent of expansion change
                if (onTreeExpansionChanged)
                    onTreeExpansionChanged(isTreeManuallyExpanded);

                // Trigger repaint of top-level component for layout updates
                if (auto* topLevel = getTopLevelComponent())
                {
                    topLevel->repaint();
                    topLevel->resized();
                }

                return;
            }
        }

        // Check if the click originated from the tree view or its children
        if (e.eventComponent == &treeView || treeView.isParentOf(e.eventComponent))
        {
            // Check if the click is on a scrollbar - if so, ignore it for collapse/expand logic
            auto localPos = e.getEventRelativeTo(&treeView).getPosition();
            bool isScrollbarClick = localPos.x >= treeView.getWidth() - 16; // Scrollbar area on right

            if (!isScrollbarClick)
            {
                // In collapsed mode - expand when clicking anywhere on the visible first line
                if (isInCollapsedMode())
                {
                    toggleManualExpansion();
                    return;
                }
                // In expanded mode - check if clicking in the first line (top 24px)
                // to allow collapsing by clicking the same area again
                else if (isTreeManuallyExpanded)
                {
                    if (localPos.y < 24) // Top 24px is the first tree line
                    {
                        toggleManualExpansion();
                        return;
                    }
                    // Otherwise, let the tree handle the click normally (selecting items, etc.)
                }
            }
        }
    }

    // Default handling
    Component::mouseDown(e);
}

void SearchableTreeView::parentHierarchyChanged()
{
    Component::parentHierarchyChanged();

    // Set up global mouse listener for click-away detection
    if (isParentOf(&treeView) && getParentComponent() != nullptr)
    {
        if (auto* topLevel = getTopLevelComponent())
            topLevel->addMouseListener(this, true);
    }
}

bool SearchableTreeView::hitTest(int x, int y)
{
    // In auto-hide mode when collapsed, accept hits in search field AND tree preview area
    if (autoHideTreeWithoutResults && isInCollapsedMode())
    {
        // Accept hits in search field (0-38) and tree area (38+)
        return true; // Accept all hits when collapsed to allow search field interaction
    }

    // Otherwise use default hit testing
    bool result = Component::hitTest(x, y);
    DBG(
        "SearchableTreeView::hitTest(" << x << ", " << y << ") - normal mode, returning " << (result ? "true" : "false")
    );
    return result;
}
