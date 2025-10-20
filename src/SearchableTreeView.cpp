#include "SearchableTreeView.h"
#include "JsfxPluginTreeView.h"
#include <juce_audio_processors/juce_audio_processors.h>

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

void SearchableTreeItem::paintItem(juce::Graphics& g, int width, int height)
{
    // Draw highlight backgrounds (selected, focused, matched states)
    paintMatchHighlight(g, width, height);

    // Draw item name in white with standard formatting
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(getName(), 4, 0, width - 8, height, juce::Justification::centredLeft, true);
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

    // Handle ESC key - collapse tree and clear search
    if (key == juce::KeyPress::escapeKey && treeView)
    {
        treeView->handleEscapeKey();
        return true;
    }

    // Let TextEditor handle all other keys (including up arrow for cursor movement)
    return juce::TextEditor::keyPressed(key);
}

// ============================================================================
// FilteredTreeView implementation
// ============================================================================

void FilteredTreeView::ClickAwayListener::mouseDown(const juce::MouseEvent& e)
{
    // Get click position relative to the tree viewport
    auto posRelativeToTree = e.getEventRelativeTo(&treeView).getPosition();

    // If click is outside the tree viewport bounds, collapse it
    // Use getLocalBounds() which gives us (0,0,width,height) in the tree's coordinate space
    if (!treeView.getLocalBounds().contains(posRelativeToTree))
    {
        if (treeView.searchView)
            treeView.searchView->toggleManualExpansion();
    }
}

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

    // Handle ESC key - collapse tree and move to search field
    if (key == juce::KeyPress::escapeKey && searchView)
    {
        searchView->handleEscapeFromTree();
        return true;
    }

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

    // Check if this is a navigation key (up/down arrows only)
    bool isNavigationKey = (keyCode == juce::KeyPress::upKey || keyCode == juce::KeyPress::downKey);

    // Get modifier state once for reuse
    auto modifiers = juce::ModifierKeys::currentModifiers;

    // Show immediate focus indicator when Ctrl+navigation key pressed (without Shift)
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
            searchView->executeCommand(selectedItems);
            return true;
        }
    }

    // Handle Ctrl+Space for toggling selection of current item
    if (keyCode == juce::KeyPress::spaceKey && modifiers.isCtrlDown())
    {
        // Toggle the focused item if it exists, otherwise use first selected item
        juce::TreeViewItem* currentItem = focusedItem ? focusedItem : findFirstSelected();

        if (currentItem && currentItem->canBeSelected())
        {
            currentItem->setSelected(!currentItem->isSelected(), false);
            return true;
        }
    }

    // Handle up/down navigation (arrow keys)
    if (isNavigationKey)
    {
        // Get current modifier state
        bool shiftHeld = modifiers.isShiftDown();
        bool ctrlHeld = modifiers.isCtrlDown();
        bool isDown = (keyCode == juce::KeyPress::downKey);

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

    // Handle left/right navigation (arrow keys only) for expanding/collapsing
    if (keyCode == juce::KeyPress::leftKey || keyCode == juce::KeyPress::rightKey)
    {
        bool isRight = (keyCode == juce::KeyPress::rightKey);

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

    // Handle alphanumeric keys and backspace - move focus to search field
    if (searchView)
    {
        auto textChar = key.getTextCharacter();
        if (juce::CharacterFunctions::isLetterOrDigit(textChar))
        {
            searchView->moveFocusToSearchField();
            searchView->insertTextIntoSearchField(juce::String::charToString(textChar));
            return true;
        }

        // Handle backspace key
        if (key == juce::KeyPress::backspaceKey)
        {
            searchView->moveFocusToSearchField();
            // Simulate backspace in the search field by removing the last character
            auto currentText = searchView->getSearchText();
            if (currentText.isNotEmpty())
                searchView->setSearchText(currentText.dropLastCharacters(1));
            return true;
        }
    }

    bool result = juce::TreeView::keyPressed(key);

    return result;
}

void FilteredTreeView::mouseDown(const juce::MouseEvent& e)
{
    // If in collapsed mode, expand the tree
    if (searchView && searchView->isAutoHideEnabled() && searchView->isInCollapsedMode())
    {
        searchView->toggleManualExpansion();
        return;
    }

    // Otherwise, let TreeView handle the click normally
    juce::TreeView::mouseDown(e);
}

void FilteredTreeView::resized()
{
    // In overlay mode, manage viewport ourselves to eliminate padding
    if (isOverlayMode)
    {
        if (auto* viewport = getViewport())
        {
            // Don't call base class - manage viewport directly
            viewport->setBounds(getLocalBounds());
        }
    }
    else
    {
        // Normal mode - use base class layout
        juce::TreeView::resized();
    }
}

void FilteredTreeView::paint(juce::Graphics& g)
{
    if (isOverlayMode)
    {
        // Paint TreeView's own background color
        g.fillAll(getLookAndFeel().findColour(juce::TreeView::backgroundColourId));

        // Draw border for visibility
        g.setColour(getLookAndFeel().findColour(juce::ComboBox::outlineColourId));
        g.drawRect(getLocalBounds(), 1);
    }

    // Always call parent to paint tree content
    juce::TreeView::paint(g);
}

void FilteredTreeView::paintOverChildren(juce::Graphics& g)
{
    // Call base class first
    juce::TreeView::paintOverChildren(g);

    // If this is a JsfxPluginTreeView, draw download glow effects on top
    if (auto* jsfxTreeView = dynamic_cast<JsfxPluginTreeView*>(searchView))
        jsfxTreeView->drawDownloadGlowEffects(g);
}

juce::AudioProcessorEditor* FilteredTreeView::findAudioProcessorEditor()
{
    juce::Component* current = getParentComponent();
    while (current != nullptr)
    {
        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(current))
            return editor;
        current = current->getParentComponent();
    }
    return nullptr;
}

void FilteredTreeView::expandAsOverlay()
{
    // Find the AudioProcessorEditor
    auto* editor = findAudioProcessorEditor();
    if (!editor)
        return;

    // Already in overlay mode
    if (overlayParent == editor && getParentComponent() == editor)
        return;

    // Get current position in editor coordinates BEFORE reparenting
    auto currentPosInEditor = editor->getLocalPoint(this, juce::Point<int>(0, 0));

    // Save original state
    originalParent = getParentComponent();
    originalBounds = getBounds();
    overlayParent = editor;

    // Remove from current parent and add to editor
    if (originalParent)
        originalParent->removeChildComponent(this);

    editor->addAndMakeVisible(*this);
    isOverlayMode = true;

    // Calculate ideal dimensions based on tree content
    int idealWidth = 400; // Start with reasonable default
    int idealHeight = 300;

    if (searchView)
    {
        // Get ideal dimensions from SearchableTreeView
        idealHeight = searchView->getIdealTreeHeight();
        idealWidth = searchView->getIdealTreeWidth();
    }

    // Add some padding for scrollbar and borders
    idealWidth += 20;
    idealHeight += 20;

    // Constrain to available space in editor
    int margin = 10;
    int maxWidth = editor->getWidth() - currentPosInEditor.x - margin;
    int maxHeight = editor->getHeight() - currentPosInEditor.y - margin;

    int finalWidth = juce::jmin(idealWidth, maxWidth);
    int finalHeight = juce::jmin(idealHeight, maxHeight);

    // Position at exact same location, with ideal size constrained by available space
    setBounds(currentPosInEditor.x, currentPosInEditor.y, finalWidth, finalHeight);

    // Re-enable viewport mouse clicks for normal tree interaction
    if (auto* viewport = getViewport())
    {
        viewport->setInterceptsMouseClicks(true, true);
        viewport->setViewPosition(0, 0);
    }

    // Trigger layout update - our resized() override will ensure viewport fills the space
    resized();

    // Add click-away listener to editor for collapsing when clicking outside
    if (editor)
        editor->addMouseListener(&clickAwayListener, true);

    toFront(false);
    repaint();
}

void FilteredTreeView::collapseFromOverlay()
{
    // Only collapse if actually in overlay mode
    if (!overlayParent || !originalParent)
        return;

    // Check if we're actually a child of the overlay parent
    if (getParentComponent() != overlayParent)
        return;

    // Remove click-away listener from editor
    if (overlayParent)
        overlayParent->removeMouseListener(&clickAwayListener);

    // Remove from editor
    overlayParent->removeChildComponent(this);
    isOverlayMode = false;

    // Restore to original parent
    originalParent->addAndMakeVisible(*this);
    setBounds(originalBounds);

    // Clear overlay state
    overlayParent = nullptr;
    originalParent = nullptr;
}

bool FilteredTreeView::hitTest(int x, int y)
{
    // In collapsed hint line mode, accept all hits so we can handle the click
    if (searchView && searchView->isAutoHideEnabled() && searchView->isInCollapsedMode())
        return true; // Accept all mouse events in collapsed mode

    // Normal mode - use default TreeView hit testing
    return juce::TreeView::hitTest(x, y);
}

// ============================================================================
// SearchableTreeView implementation
// ============================================================================

// Static registry for global Ctrl+F cycling
juce::Array<SearchableTreeView*>& SearchableTreeView::getAllInstances()
{
    static juce::Array<SearchableTreeView*> instances;
    return instances;
}

void SearchableTreeView::focusNextSearchField()
{
    auto& instances = getAllInstances();
    if (instances.isEmpty())
        return;

    // Find currently focused instance
    int currentIndex = -1;
    for (int i = 0; i < instances.size(); ++i)
    {
        if (instances[i]->isSearchFieldFocused())
        {
            currentIndex = i;
            break;
        }
    }

    // Move to next instance (wrap around)
    int nextIndex = (currentIndex + 1) % instances.size();
    if (auto* nextInstance = instances[nextIndex])
        nextInstance->moveFocusToSearchField();
}

void SearchableTreeView::collapseAllExpandedTrees()
{
    auto& instances = getAllInstances();
    for (auto* instance : instances)
        if (instance && instance->isTreeManuallyExpanded)
            instance->collapseTree();
}

bool SearchableTreeView::isSearchFieldFocused() const
{
    return searchField.hasKeyboardFocus(true);
}

SearchableTreeView::SearchableTreeView()
{
    // Register this instance
    getAllInstances().add(this);

    // Setup search field
    addAndMakeVisible(searchField);
    searchField.setTextToShowWhenEmpty("Type to search...", juce::Colours::grey);
    searchField.addListener(this);
    searchField.setWantsKeyboardFocus(true);
    searchField.setSearchableTreeView(this);

    // Setup browse button
    addAndMakeVisible(browseButton);
    browseButton.setButtonText("Browse...");
    browseButton.onClick = [this]
    {
        // Use custom callback if set, otherwise show default browse menu
        if (customBrowseCallback)
            customBrowseCallback();
        else
            showBrowseMenu();
    };

    // Setup tree view
    addAndMakeVisible(treeView);
    treeView.setMultiSelectEnabled(true);
    treeView.setRootItemVisible(false);
    treeView.setWantsKeyboardFocus(true);
    treeView.setSearchableTreeView(this);
    treeView.setInterceptsMouseClicks(true, true); // Ensure tree receives all mouse events
    // Don't add mouse listener here - let FilteredTreeView handle its own mouse events
    // treeView.addMouseListener(this, true);

    // Setup metadata label
    addAndMakeVisible(metadataLabel);
    metadataLabel.setJustificationType(juce::Justification::centred);
    metadataLabel.setColour(juce::Label::textColourId, juce::Colours::grey);

    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true); // Ensure we receive mouse events for click-away detection
}

SearchableTreeView::~SearchableTreeView()
{
    // Unregister this instance
    getAllInstances().removeAllInstancesOf(this);

    treeView.setRootItem(nullptr);
}

void SearchableTreeView::paint(juce::Graphics& g)
{
    // Don't paint background in tree area when tree is in overlay mode
    if (autoHideTreeWithoutResults && treeView.isOverlayMode)
    {
        // Only paint the search field area background
        auto bounds = getLocalBounds();
        if (showSearchField)
        {
            auto searchArea = bounds.removeFromTop(30); // Search field + spacing
            g.setColour(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
            g.fillRect(searchArea);
        }
    }
    else
    {
        // Normal mode - paint entire background
        g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    }
}

void SearchableTreeView::resized()
{
    auto bounds = getLocalBounds();

    // Search bar at top (if visible)
    if (showSearchField)
    {
        auto searchArea = bounds.removeFromTop(25); // Half the previous height (50 -> 25)

        // Show browse button only in auto-hide mode
        if (autoHideTreeWithoutResults)
        {
            // Limit button width to ensure text is visible (max width that fits "Browse...")
            auto buttonWidth = juce::jmin(80, searchArea.getWidth() / 3);
            browseButton.setBounds(searchArea.removeFromRight(buttonWidth));
            searchArea.removeFromRight(5); // Spacing between search field and button
            browseButton.setVisible(true);
        }
        else
        {
            browseButton.setVisible(false);
        }

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
            // Expanded: let FilteredTreeView attach itself to AudioProcessorEditor as overlay
            treeView.expandAsOverlay();
        }
        else
        {
            // Collapsed: let FilteredTreeView return to normal parent and show preview
            treeView.collapseFromOverlay();

            int itemHeight = 24; // Standard tree item height
            auto treeArea = bounds.withHeight(itemHeight);
            treeView.setBounds(treeArea);
            treeView.setVisible(true);
            treeView.setInterceptsMouseClicks(true, false); // Only tree itself gets clicks, not children

            // Disable mouse clicks on viewport so they go to the tree instead
            if (auto* viewport = treeView.getViewport())
            {
                viewport->setViewPosition(0, 0);
                viewport->setInterceptsMouseClicks(false, false);
            }
        }
    }
    else
    {
        // Normal mode: ensure not in overlay, then use all remaining space
        treeView.collapseFromOverlay();
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

        // Only execute command if enabled for search field
        if (triggerCommandFromSearchField)
        {
            executeCommand(selectedItems);
        }
        else if (selectedItems.size() == 1)
        {
            // Call the virtual method for single item (backward compatibility)
            // Only if command wasn't already triggered
            onEnterKeyPressed(selectedItems[0]);
        }
    }
}

void SearchableTreeView::moveFocusToTree()
{
    // Don't move focus if tree has no visible selectable items
    if (!hasVisibleSelectableItems())
        return;

    // If in auto-hide collapsed mode, expand the tree
    if (autoHideTreeWithoutResults && isInCollapsedMode() && !isTreeManuallyExpanded)
    {
        isTreeManuallyExpanded = true;

        // Open all root level items
        if (rootItem)
        {
            for (int i = 0; i < rootItem->getNumSubItems(); ++i)
                if (auto* item = rootItem->getSubItem(i))
                    item->setOpen(true);
        }

        // Update layout
        resized();

        // Notify parent of expansion change
        if (onTreeExpansionChanged)
            onTreeExpansionChanged(true);

        // Trigger repaint
        treeView.repaint();
        repaint();

        // Trigger repaint of top-level component for layout updates
        if (auto* topLevel = getTopLevelComponent())
            topLevel->repaint();
    }

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

void SearchableTreeView::insertTextIntoSearchField(const juce::String& text)
{
    searchField.insertTextAtCaret(text);
}

void SearchableTreeView::executeCommand(const juce::Array<juce::TreeViewItem*>& selectedItems)
{
    if (selectedItems.isEmpty())
        return;

    // Call the command callback if set
    if (onCommand)
        onCommand(selectedItems);

    // Also call virtual method for single item (for actual action handling)
    if (selectedItems.size() == 1)
        onEnterKeyPressed(selectedItems[0]);
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

bool SearchableTreeView::hasVisibleSelectableItems() const
{
    if (!rootItem)
        return false;

    // Helper to recursively check for visible selectable items
    std::function<bool(juce::TreeViewItem*)> hasVisibleItems = [&](juce::TreeViewItem* item) -> bool
    {
        if (!item)
            return false;

        // Check if this item is visible and selectable
        auto* searchableItem = dynamic_cast<SearchableTreeItem*>(item);
        if (searchableItem && !searchableItem->getHidden() && item->canBeSelected())
            return true;

        // Check children
        for (int i = 0; i < item->getNumSubItems(); ++i)
            if (hasVisibleItems(item->getSubItem(i)))
                return true;

        return false;
    };

    // Check all root level items
    for (int i = 0; i < rootItem->getNumSubItems(); ++i)
        if (hasVisibleItems(rootItem->getSubItem(i)))
            return true;

    return false;
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

    // Count visible items and sum their actual heights
    int totalHeight = 0;
    int itemCount = 0;
    std::function<void(juce::TreeViewItem*)> sumHeights = [&](juce::TreeViewItem* item)
    {
        if (!item)
            return;

        auto* searchableItem = dynamic_cast<SearchableTreeItem*>(item);
        if (!searchableItem || !searchableItem->getHidden())
        {
            // Add this item's actual height
            totalHeight += item->getItemHeight();
            itemCount++;
        }

        // Process children if item is open
        if (item->isOpen())
            for (int i = 0; i < item->getNumSubItems(); ++i)
                sumHeights(item->getSubItem(i));
    };

    for (int i = 0; i < rootItem->getNumSubItems(); ++i)
        sumHeights(rootItem->getSubItem(i));

    // Add padding
    int calculatedHeight = totalHeight + 20;

    // For auto-hide mode, return calculated height (with reasonable max)
    // Don't limit based on parent height to avoid circular dependency
    if (autoHideTreeWithoutResults)
    {
        // Apply a reasonable maximum to prevent absurd heights
        // Minimum is 50px (enough for 2-3 items), maximum is 800px
        return juce::jlimit(50, 800, calculatedHeight);
    }

    // In normal mode, return the calculated height (parent will constrain it)
    return calculatedHeight;
}

int SearchableTreeView::getIdealTreeWidth() const
{
    if (!rootItem)
        return 400;

    // Find the widest visible item by measuring text
    int maxWidth = 300;     // Minimum width
    juce::Font font(14.0f); // Match the font used in SearchableTreeItem::paintItem
    int indentSize = 20;    // Default indent size for TreeView

    std::function<void(juce::TreeViewItem*, int)> measureItems = [&](juce::TreeViewItem* item, int depth)
    {
        if (!item)
            return;

        auto* searchableItem = dynamic_cast<SearchableTreeItem*>(item);
        if (!searchableItem || searchableItem->getHidden())
            return; // Skip hidden items

        // Calculate text width including indentation
        int indentWidth = depth * indentSize;
        int textWidth = font.getStringWidth(searchableItem->getName());
        // More generous padding: indent + text + icon space + right padding + scrollbar room
        int totalWidth = indentWidth + textWidth + 80;

        maxWidth = juce::jmax(maxWidth, totalWidth);

        // Process children if item is open
        if (item->isOpen())
            for (int i = 0; i < item->getNumSubItems(); ++i)
                measureItems(item->getSubItem(i), depth + 1);
    };

    for (int i = 0; i < rootItem->getNumSubItems(); ++i)
        measureItems(rootItem->getSubItem(i), 0);

    // Return constrained width (min 300, max 1000 for wider displays)
    return juce::jlimit(300, 1000, maxWidth);
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
    // Prevent rapid multiple toggles (debounce)
    static juce::uint32 lastToggleTime = 0;
    auto currentTime = juce::Time::getMillisecondCounter();
    if (currentTime - lastToggleTime < 200) // 200ms debounce
        return;

    lastToggleTime = currentTime;

    // Toggle expansion state
    isTreeManuallyExpanded = !isTreeManuallyExpanded;

    if (rootItem)
    {
        if (isTreeManuallyExpanded)
        {
            // Expanding - open entire tree recursively FIRST, then resize

            // Helper lambda to recursively open all items
            std::function<void(juce::TreeViewItem*)> openAllItems = [&](juce::TreeViewItem* item)
            {
                if (!item)
                    return;

                item->setOpen(true);

                for (int i = 0; i < item->getNumSubItems(); ++i)
                    openAllItems(item->getSubItem(i));
            };

            for (int i = 0; i < rootItem->getNumSubItems(); ++i)
                if (auto* item = rootItem->getSubItem(i))
                    openAllItems(item);

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

    // Update layout AFTER tree items are expanded/collapsed
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

void SearchableTreeView::handleEscapeKey()
{
    // Called from search field - clear search and lose focus

    // Clear the search field
    searchField.setText("", true); // true = send notification to trigger filtering/collapse
    currentSearchTerm = "";

    // Move focus away from search field immediately
    if (searchField.hasKeyboardFocus(true))
        searchField.giveAwayKeyboardFocus();

    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();
}

void SearchableTreeView::handleEscapeFromTree()
{
    // Called from tree view - collapse tree and move focus to search field

    // Collapse the tree if it's manually expanded
    if (isTreeManuallyExpanded)
    {
        isTreeManuallyExpanded = false;

        // Collapse all root level items
        if (rootItem)
        {
            for (int i = 0; i < rootItem->getNumSubItems(); ++i)
                if (auto* item = rootItem->getSubItem(i))
                    item->setOpen(false);
        }

        // Clear selection
        treeView.clearSelectedItems();

        // Update layout
        resized();

        // Notify parent of expansion change
        if (onTreeExpansionChanged)
            onTreeExpansionChanged(false);

        // Trigger repaint
        treeView.repaint();
        repaint();

        // Trigger repaint of top-level component for layout updates
        if (auto* topLevel = getTopLevelComponent())
        {
            topLevel->repaint();
            topLevel->resized();
        }
    }

    // Move focus to search field
    moveFocusToSearchField();
}

void SearchableTreeView::collapseTree()
{
    // Collapse the tree if it's manually expanded
    if (isTreeManuallyExpanded)
    {
        isTreeManuallyExpanded = false;

        // Collapse all root level items
        if (rootItem)
        {
            for (int i = 0; i < rootItem->getNumSubItems(); ++i)
                if (auto* item = rootItem->getSubItem(i))
                    item->setOpen(false);
        }

        // Keep selection intact - don't clear it

        // Update layout
        resized();

        // Notify parent of expansion change
        if (onTreeExpansionChanged)
            onTreeExpansionChanged(false);

        // Trigger repaint
        treeView.repaint();
        repaint();

        // Trigger repaint of top-level component for layout updates
        if (auto* topLevel = getTopLevelComponent())
        {
            topLevel->repaint();
            topLevel->resized();
        }
    }
}

void SearchableTreeView::parentHierarchyChanged()
{
    Component::parentHierarchyChanged();
}

bool SearchableTreeView::hitTest(int x, int y)
{
    // When tree is in overlay mode (expanded), only accept hits in the search field area
    if (autoHideTreeWithoutResults && treeView.isOverlayMode)
    {
        // Only accept hits in the search field area at the top
        if (showSearchField && y < 30) // Search field height
            return true;

        // Reject all other hits - let them pass through to the overlay tree
        return false;
    }

    // Otherwise use default hit testing (collapsed mode or normal mode)
    return Component::hitTest(x, y);
}

// ============================================================================
// Browse Menu Implementation
// ============================================================================

juce::Array<juce::TreeViewItem*> SearchableTreeView::getDeepestLevelItems()
{
    juce::Array<juce::TreeViewItem*> items;

    std::function<void(juce::TreeViewItem*)> collectLeafItems = [&](juce::TreeViewItem* item)
    {
        if (!item)
            return;

        int numChildren = item->getNumSubItems();
        if (numChildren == 0)
        {
            // Leaf item
            items.add(item);
        }
        else
        {
            // Recurse into children
            for (int i = 0; i < numChildren; ++i)
                collectLeafItems(item->getSubItem(i));
        }
    };

    if (rootItem)
        collectLeafItems(rootItem.get());

    return items;
}

juce::String SearchableTreeView::getParentCategoryForItem(juce::TreeViewItem* item)
{
    if (!item)
        return {};

    // Default: use parent item's name, or "Uncategorized" if no parent
    auto* parent = item->getParentItem();
    if (parent)
    {
        if (auto* searchableParent = dynamic_cast<SearchableTreeItem*>(parent))
            return searchableParent->getName();
    }

    return "Uncategorized";
}

void SearchableTreeView::showBrowseMenu()
{
    auto deepestItems = getDeepestLevelItems();

    if (deepestItems.isEmpty())
        return;

    // Organize items by parent category
    juce::HashMap<juce::String, juce::Array<juce::TreeViewItem*>> itemsByCategory;

    for (auto* item : deepestItems)
    {
        juce::String category = getParentCategoryForItem(item);
        if (!itemsByCategory.contains(category))
            itemsByCategory.set(category, {});
        itemsByCategory.getReference(category).add(item);
    }

    // Build sorted list of categories
    juce::StringArray categories;
    for (auto it = itemsByCategory.begin(); it != itemsByCategory.end(); ++it)
        categories.add(it.getKey());
    categories.sort(true); // Case-insensitive sort

    // Build the popup menu and create itemId -> TreeViewItem mapping
    juce::PopupMenu menu;
    int itemId = 1;
    auto itemIdToTreeItem = std::make_shared<juce::HashMap<int, juce::TreeViewItem*>>();
    buildBrowseMenu(menu, categories, itemsByCategory, itemId, 0, *itemIdToTreeItem);

    // Show menu below the browse button
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&browseButton).withMaximumNumColumns(4),
        [this, itemIdToTreeItem](int result)
        {
            if (result > 0 && itemIdToTreeItem->contains(result))
            {
                auto* selectedItem = (*itemIdToTreeItem)[result];
                if (selectedItem)
                {
                    // Deselect all items first
                    if (rootItem)
                    {
                        std::function<void(juce::TreeViewItem*)> deselectAll = [&](juce::TreeViewItem* item)
                        {
                            item->setSelected(false, false);
                            for (int i = 0; i < item->getNumSubItems(); ++i)
                                deselectAll(item->getSubItem(i));
                        };
                        deselectAll(rootItem.get());
                    }

                    // Select the chosen item
                    selectedItem->setSelected(true, true);

                    // Execute command on the selected item
                    juce::Array<juce::TreeViewItem*> selection;
                    selection.add(selectedItem);
                    executeCommand(selection);
                }
            }
        }
    );
}

void SearchableTreeView::buildBrowseMenu(
    juce::PopupMenu& menu,
    const juce::StringArray& categories,
    const juce::HashMap<juce::String, juce::Array<juce::TreeViewItem*>>& itemsByCategory,
    int& itemId,
    int categoryIndex,
    juce::HashMap<int, juce::TreeViewItem*>& itemIdToTreeItem
)
{
    // Maximum items per column in multi-column menu
    const int maxItemsPerColumn = 25;
    // Maximum columns per submenu (4 columns = 100 items max per submenu)
    const int maxColumns = 4;
    const int maxItemsPerSubmenu = maxItemsPerColumn * maxColumns;

    if (categoryIndex >= categories.size())
        return;

    const juce::String& category = categories[categoryIndex];
    auto items = itemsByCategory[category];

    if (items.size() <= maxItemsPerSubmenu)
    {
        // Single multi-column submenu for this category
        juce::PopupMenu categoryMenu;

        // Calculate optimal number of columns
        int numColumns = (items.size() + maxItemsPerColumn - 1) / maxItemsPerColumn;
        numColumns = juce::jmin(numColumns, maxColumns);

        for (auto* item : items)
        {
            if (auto* searchableItem = dynamic_cast<SearchableTreeItem*>(item))
            {
                categoryMenu.addItem(itemId, searchableItem->getName());
                itemIdToTreeItem.set(itemId, item);
                itemId++;
            }
        }

        menu.addSubMenu(category, categoryMenu, true, nullptr, false, numColumns);
    }
    else
    {
        // Split into multiple numbered submenus, each with up to 4 columns
        int numSplits = (items.size() + maxItemsPerSubmenu - 1) / maxItemsPerSubmenu;

        for (int split = 0; split < numSplits; ++split)
        {
            juce::PopupMenu splitMenu;

            int startIdx = split * maxItemsPerSubmenu;
            int endIdx = juce::jmin(startIdx + maxItemsPerSubmenu, items.size());
            int splitSize = endIdx - startIdx;

            // Calculate optimal number of columns for this split
            int numColumns = (splitSize + maxItemsPerColumn - 1) / maxItemsPerColumn;
            numColumns = juce::jmin(numColumns, maxColumns);

            for (int i = startIdx; i < endIdx; ++i)
            {
                if (auto* searchableItem = dynamic_cast<SearchableTreeItem*>(items[i]))
                {
                    splitMenu.addItem(itemId, searchableItem->getName());
                    itemIdToTreeItem.set(itemId, items[i]);
                    itemId++;
                }
            }

            juce::String splitName = category + " (" + juce::String(split + 1) + ")";
            menu.addSubMenu(splitName, splitMenu, true, nullptr, false, numColumns);
        }
    }

    // Recursively add remaining categories
    buildBrowseMenu(menu, categories, itemsByCategory, itemId, categoryIndex + 1, itemIdToTreeItem);
}
