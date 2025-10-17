#include "LibraryBrowser.h"

//==============================================================================
// Static member initialization
LibraryBrowser* LibraryBrowser::activeWasdInstance = nullptr;

//==============================================================================
juce::PopupMenu::Options
LibraryBrowser::BrowserLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label)
{
    auto opts = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label);
    return opts.withMaximumNumColumns(4);
}

//==============================================================================
// FilteredListPopup implementation
LibraryBrowser::FilteredListPopup::FilteredListPopup(LibraryBrowser& o)
    : owner(o)
{
    setWantsKeyboardFocus(true);
    setAlwaysOnTop(true);
    setOpaque(true);
}

void LibraryBrowser::FilteredListPopup::setItems(const std::vector<Item>& items)
{
    itemList = items;
    selectedIndex = -1;
    scrollOffset = 0; // Reset scroll when new items are set

    auto& lf = getLookAndFeel();
    auto opts = juce::PopupMenu::Options();
    int standardHeight = opts.getStandardItemHeight();
    int borderSize = lf.getPopupMenuBorderSizeWithOptions(opts);

    // Calculate sizes exactly like JUCE PopupMenu does
    int maxWidth = standardHeight; // Start with standardHeight like JUCE
    int maxHeight = 0;

    for (const auto& item : itemList)
    {
        int itemW = 80;
        int itemH = 16;

        // Use JUCE's method to calculate ideal size for each item
        lf.getIdealPopupMenuItemSizeWithOptions(
            item.label,
            item.isHeader, // isSeparator
            standardHeight,
            itemW,
            itemH,
            opts
        );

        maxWidth = juce::jmax(maxWidth, itemW);
        maxHeight = juce::jmax(maxHeight, itemH);
    }

    // Add border * 2 to width (like JUCE does)
    idealWidth = maxWidth + borderSize * 2;

    // Use the calculated item height (limited like JUCE does)
    itemHeight = juce::jlimit(1, 600, maxHeight);

    // Calculate total content height
    contentHeight = (int)itemList.size() * itemHeight;

    // Auto-select first selectable item
    for (int i = 0; i < (int)itemList.size(); ++i)
    {
        if (!itemList[i].isHeader)
        {
            selectedIndex = i;
            break;
        }
    }

    repaint();
}

void LibraryBrowser::FilteredListPopup::show(juce::Component& attachTo)
{
    DBG("FilteredListPopup::show called");

    // Get the top-level component (desktop)
    auto* topLevel = attachTo.getTopLevelComponent();
    DBG("Top level component: " << (topLevel ? "valid" : "null"));

    if (topLevel)
    {
        DBG("Adding popup to top-level component");
        topLevel->addAndMakeVisible(this);

        // Add mouse listener to desktop to catch clicks outside
        topLevel->addMouseListener(this, true);

        // Get screen position of textEditor
        auto screenBounds = attachTo.getScreenBounds();

        // Convert to top-level component coordinates
        auto topLevelBounds = topLevel->getLocalArea(nullptr, screenBounds);

        // Calculate height: content + borders, but limited to max height (like JUCE's 600px limit)
        auto opts = juce::PopupMenu::Options();
        int borderSize = getLookAndFeel().getPopupMenuBorderSizeWithOptions(opts);
        int maxHeight = 600; // Same as JUCE PopupMenu
        int height = juce::jmin(contentHeight + borderSize * 2, maxHeight);

        // Use calculated ideal width, but respect minimum width from textEditor
        int width = juce::jmax(idealWidth, topLevelBounds.getWidth());

        DBG("Setting bounds: x="
            << topLevelBounds.getX()
            << ", y="
            << topLevelBounds.getBottom()
            << ", width="
            << width
            << ", height="
            << height);
        setBounds(topLevelBounds.getX(), topLevelBounds.getBottom(), width, height);

        DBG("Calling toFront (without grabbing focus)");
        toFront(false); // Don't grab focus - let text editor keep it
        repaint();
        DBG("FilteredListPopup::show completed");
    }
}

void LibraryBrowser::FilteredListPopup::hide()
{
    if (auto* parent = getParentComponent())
    {
        parent->removeMouseListener(this);
        parent->removeChildComponent(this);
    }
}

bool LibraryBrowser::FilteredListPopup::isVisible() const
{
    return getParentComponent() != nullptr;
}

void LibraryBrowser::FilteredListPopup::selectNext()
{
    for (int i = selectedIndex + 1; i < (int)itemList.size(); ++i)
    {
        if (!itemList[i].isHeader)
        {
            selectedIndex = i;
            ensureSelectedVisible();
            repaint();
            return;
        }
    }
}

void LibraryBrowser::FilteredListPopup::selectPrevious()
{
    for (int i = selectedIndex - 1; i >= 0; --i)
    {
        if (!itemList[i].isHeader)
        {
            selectedIndex = i;
            ensureSelectedVisible();
            repaint();
            return;
        }
    }
}

void LibraryBrowser::FilteredListPopup::selectCurrent()
{
    if (selectedIndex >= 0 && selectedIndex < (int)itemList.size() && !itemList[selectedIndex].isHeader)
    {
        owner.onFilteredItemSelected(itemList[selectedIndex].index);
        hide();
    }
}

void LibraryBrowser::FilteredListPopup::ensureSelectedVisible()
{
    if (selectedIndex >= 0 && selectedIndex < (int)itemList.size())
    {
        auto opts = juce::PopupMenu::Options();
        int borderSize = getLookAndFeel().getPopupMenuBorderSizeWithOptions(opts);
        int visibleHeight = getHeight() - borderSize * 2;

        int itemTop = selectedIndex * itemHeight;
        int itemBottom = itemTop + itemHeight;

        int visibleTop = scrollOffset;
        int visibleBottom = scrollOffset + visibleHeight;

        if (itemTop < visibleTop)
        {
            // Scroll up to show item
            scrollOffset = itemTop;
        }
        else if (itemBottom > visibleBottom)
        {
            // Scroll down to show item
            scrollOffset = itemBottom - visibleHeight;
        }

        // Clamp scroll offset
        int maxScroll = contentHeight - visibleHeight;
        scrollOffset = juce::jlimit(0, juce::jmax(0, maxScroll), scrollOffset);
        repaint();
    }
}

void LibraryBrowser::FilteredListPopup::paint(juce::Graphics& g)
{
    auto& lf = getLookAndFeel();
    auto opts = juce::PopupMenu::Options();
    int borderSize = lf.getPopupMenuBorderSizeWithOptions(opts);

    // Draw background
    g.fillAll(lf.findColour(juce::PopupMenu::backgroundColourId));

    // Draw border
    g.setColour(lf.findColour(juce::PopupMenu::backgroundColourId).contrasting(0.5f));
    g.drawRect(getLocalBounds(), 1);

    // Calculate visible area (excluding borders)
    auto contentArea = getLocalBounds().reduced(borderSize, borderSize);

    // Calculate starting Y position accounting for scroll and border
    int y = borderSize - scrollOffset;

    for (int i = 0; i < (int)itemList.size(); ++i)
    {
        auto itemBounds = juce::Rectangle<int>(borderSize, y, contentArea.getWidth(), itemHeight);

        // Only draw items that are visible
        if (itemBounds.getBottom() > borderSize && itemBounds.getY() < getHeight() - borderSize)
        {
            if (itemList[i].isHeader)
            {
                // Draw section header
                g.setColour(lf.findColour(juce::PopupMenu::headerTextColourId));
                g.setFont(juce::FontOptions((float)itemHeight * 0.6f, juce::Font::bold));
                g.drawText(itemList[i].label, itemBounds.reduced(4, 0), juce::Justification::centredLeft);
            }
            else
            {
                // Draw regular item
                bool isHighlighted = (i == selectedIndex || i == hoveredIndex);

                if (isHighlighted)
                {
                    g.setColour(lf.findColour(juce::PopupMenu::highlightedBackgroundColourId));
                    g.fillRect(itemBounds);
                    g.setColour(lf.findColour(juce::PopupMenu::highlightedTextColourId));
                }
                else
                {
                    g.setColour(lf.findColour(juce::PopupMenu::textColourId));
                }

                // Use standard popup menu font
                auto font = lf.getPopupMenuFont();
                auto maxFontHeight = (float)itemHeight / 1.3f;
                if (font.getHeight() > maxFontHeight)
                    font.setHeight(maxFontHeight);
                g.setFont(font);

                // Draw text with proper padding (similar to JUCE's reduced(3) for icon area)
                g.drawText(itemList[i].label, itemBounds.reduced(8, 2), juce::Justification::centredLeft);
            }
        }

        y += itemHeight;
    }
}

void LibraryBrowser::FilteredListPopup::resized()
{
}

bool LibraryBrowser::FilteredListPopup::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::downKey)
    {
        selectNext();
        return true;
    }
    else if (key == juce::KeyPress::upKey)
    {
        // Check if we're at the first selectable item
        bool isAtFirst = true;
        for (int i = 0; i < selectedIndex; ++i)
        {
            if (!itemList[i].isHeader)
            {
                isAtFirst = false;
                break;
            }
        }

        if (isAtFirst)
        {
            // Return focus to text editor but keep popup visible
            owner.textEditor.grabKeyboardFocus();
            return true;
        }
        else
        {
            selectPrevious();
            return true;
        }
    }
    else if (key == juce::KeyPress::returnKey)
    {
        selectCurrent();
        return true;
    }
    else if (key == juce::KeyPress::escapeKey)
    {
        hide();
        owner.textEditor.grabKeyboardFocus();
        return true;
    }

    return false;
}

void LibraryBrowser::FilteredListPopup::mouseDown(const juce::MouseEvent& e)
{
    // Check if click is outside the popup - if so, hide it
    if (!getLocalBounds().contains(e.getEventRelativeTo(this).getPosition()))
    {
        hide();
        return;
    }

    auto opts = juce::PopupMenu::Options();
    int borderSize = getLookAndFeel().getPopupMenuBorderSizeWithOptions(opts);

    // Account for border and scroll when calculating index
    int index = (e.y - borderSize + scrollOffset) / itemHeight;
    if (index >= 0 && index < (int)itemList.size() && !itemList[index].isHeader)
    {
        selectedIndex = index;
        selectCurrent();
    }
}

void LibraryBrowser::FilteredListPopup::mouseMove(const juce::MouseEvent& e)
{
    auto opts = juce::PopupMenu::Options();
    int borderSize = getLookAndFeel().getPopupMenuBorderSizeWithOptions(opts);

    // Account for border and scroll when calculating index
    int index = (e.y - borderSize + scrollOffset) / itemHeight;
    if (index >= 0 && index < (int)itemList.size())
    {
        hoveredIndex = index;
        repaint();
    }
}

void LibraryBrowser::FilteredListPopup::mouseExit(const juce::MouseEvent&)
{
    hoveredIndex = -1;
    repaint();
}

void LibraryBrowser::FilteredListPopup::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    auto opts = juce::PopupMenu::Options();
    int borderSize = getLookAndFeel().getPopupMenuBorderSizeWithOptions(opts);
    int visibleHeight = getHeight() - borderSize * 2;

    // Scroll by wheel delta (negative deltaY means scroll down)
    // Use itemHeight as the scroll unit - increased multiplier for faster scrolling
    int scrollAmount = juce::roundToInt(-wheel.deltaY * itemHeight * 10.0f);
    scrollOffset += scrollAmount;

    // Clamp scroll offset
    int maxScroll = contentHeight - visibleHeight;
    scrollOffset = juce::jlimit(0, juce::jmax(0, maxScroll), scrollOffset);

    repaint();
}

//==============================================================================
LibraryBrowser::LibraryBrowser()
    : textEditor(*this) // Initialize SearchTextEditor with owner reference
{
    addAndMakeVisible(label);
    label.setText("", juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(textEditor);
    textEditor.setTextToShowWhenEmpty("", getLookAndFeel().findColour(juce::TextEditor::textColourId).withAlpha(0.5f));
    textEditor.onTextChange = [this]() { onSearchTextChanged(); };
    textEditor.onReturnKey = [this]()
    {
        if (filteredPopup && filteredPopup->isVisible())
        {
            // If only one item, select it
            filteredPopup->selectCurrent();
        }
        else
        {
            auto text = textEditor.getText();
            if (text.length() >= 3)
                showFilteredPopup(text);
            else if (text.isEmpty())
                showHierarchicalPopup();
        }
    };
    textEditor.onEscapeKey = [this]()
    {
        if (filteredPopup && filteredPopup->isVisible())
            filteredPopup->hide();
        else
            unfocusAllComponents();
    };
    textEditor.onFocusLost = [this]() {};

    addAndMakeVisible(dropdownButton);
    dropdownButton.setButtonText("v");
    dropdownButton.onClick = [this]() { showHierarchicalPopup(); };

    addAndMakeVisible(wasdToggleButton);
    wasdToggleButton.setButtonText("WASD");
    wasdToggleButton.setClickingTogglesState(true);
    wasdToggleButton.setTooltip("Enable WASD navigation: D=Next, A=Previous, S=+10, W=-10");
    wasdToggleButton.onClick = [this]() { updateWasdToggleState(wasdToggleButton.getToggleState()); };

    filteredPopup = std::make_unique<FilteredListPopup>(*this);

    setWantsKeyboardFocus(true);
}

LibraryBrowser::~LibraryBrowser()
{
    // Clear static pointer if this instance was the active one
    if (activeWasdInstance == this)
        activeWasdInstance = nullptr;
}

void LibraryBrowser::attachToValueTree(juce::ValueTree stateTree, const juce::Identifier& propertyName)
{
    parentState = stateTree;

    // Get or create the library node in the parent state
    libraryTree = parentState.getChildWithName(propertyName);
    if (!libraryTree.isValid())
    {
        libraryTree = juce::ValueTree(propertyName);
        parentState.appendChild(libraryTree, nullptr);
    }
}

void LibraryBrowser::setConverter(std::unique_ptr<PresetConverter> newConverter)
{
    converter = std::move(newConverter);
}

int LibraryBrowser::loadLibrary(const juce::String& directoryPath, bool recursive, bool clearExisting)
{
    juce::StringArray paths;
    paths.add(directoryPath);
    return loadLibrary(paths, recursive, clearExisting);
}

int LibraryBrowser::loadLibrary(const juce::StringArray& directoryPaths, bool recursive, bool clearExisting)
{
    if (!converter)
    {
        DBG("LibraryBrowser::loadLibrary - No converter set!");
        return 0;
    }

    if (!libraryTree.isValid())
    {
        DBG("LibraryBrowser::loadLibrary - Not attached to ValueTree!");
        return 0;
    }

    if (clearExisting)
        libraryTree.removeAllChildren(nullptr);

    juce::Array<juce::File> allFiles;
    for (const auto& path : directoryPaths)
    {
        auto files = scanFiles(path, recursive);
        allFiles.addArray(files);
    }

    return loadFilesIntoTree(allFiles);
}

void LibraryBrowser::clearLibrary()
{
    if (libraryTree.isValid())
        libraryTree.removeAllChildren(nullptr);
}

juce::Array<juce::File> LibraryBrowser::scanFiles(const juce::String& directoryPath, bool recursive)
{
    juce::Array<juce::File> results;

    if (!converter)
        return results;

    juce::File directory(directoryPath);
    if (!directory.exists() || !directory.isDirectory())
        return results;

    auto extensions = converter->getSupportedExtensions();
    juce::String wildcardPattern;
    for (int i = 0; i < extensions.size(); ++i)
    {
        if (i > 0)
            wildcardPattern += ";";
        wildcardPattern += "*" + extensions[i];
    }

    results = directory.findChildFiles(juce::File::findFiles, recursive, wildcardPattern);

    return results;
}

int LibraryBrowser::loadFilesIntoTree(const juce::Array<juce::File>& files)
{
    if (!converter)
        return 0;

    int successCount = 0;

    for (const auto& file : files)
    {
        auto fileNode = converter->convertFileToTree(file);
        if (fileNode.isValid())
        {
            libraryTree.appendChild(fileNode, nullptr);
            ++successCount;
        }
    }

    return successCount;
}

void LibraryBrowser::setItemSelectedCallback(ItemSelectedCallback callback)
{
    itemSelectedCallback = std::move(callback);
}

void LibraryBrowser::setLabelText(const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
}

void LibraryBrowser::setPlaceholderText(const juce::String& text)
{
    textEditor.setTextToShowWhenEmpty(text, juce::Colours::grey);
}

void LibraryBrowser::updateItemList()
{
    // Rebuild flat item list when library content changes
    buildFlatItemList();
}

void LibraryBrowser::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void LibraryBrowser::resized()
{
    auto area = getLocalBounds();
    const int labelWidth = 60;
    const int spacing = 5;
    const int buttonWidth = 20;
    const int wasdButtonWidth = 50;

    // Label on the left
    label.setBounds(area.removeFromLeft(labelWidth));
    area.removeFromLeft(spacing);

    // WASD toggle button on the far right
    wasdToggleButton.setBounds(area.removeFromRight(wasdButtonWidth));

    // Dropdown button to the left of WASD button
    dropdownButton.setBounds(area.removeFromRight(buttonWidth));
    area.removeFromRight(spacing);

    // Text editor fills the remaining space
    textEditor.setBounds(area);
}

void LibraryBrowser::onSearchTextChanged()
{
    auto text = textEditor.getText();

    if (text.length() >= 3)
    {
        // Show filtered popup with live search
        showFilteredPopup(text);
    }
    else if (text.isEmpty())
    {
        // Hide popup when text is cleared
        if (filteredPopup && filteredPopup->isVisible())
            filteredPopup->hide();
    }
    else
    {
        // Less than 3 characters - hide popup
        if (filteredPopup && filteredPopup->isVisible())
            filteredPopup->hide();
    }
}

void LibraryBrowser::showHierarchicalPopup()
{
    juce::PopupMenu menu;
    buildHierarchicalMenu(menu);
    menu.setLookAndFeel(&lookAndFeel);

    auto options = juce::PopupMenu::Options()
                       .withTargetComponent(&textEditor)
                       .withMinimumWidth(textEditor.getWidth() + dropdownButton.getWidth())
                       .withMaximumNumColumns(4);

    menu.showMenuAsync(
        options,
        [this](int result)
        {
            if (result > 0)
                onMenuResult(result);
        }
    );
}

void LibraryBrowser::showFilteredPopup(const juce::String& searchText)
{
    DBG("showFilteredPopup called with searchText: " << searchText);
    std::vector<FilteredListPopup::Item> items;
    buildFilteredList(items, searchText);

    DBG("buildFilteredList returned " << items.size() << " items");

    if (items.empty())
    {
        DBG("No items found, returning");
        return;
    }

    DBG("Calling filteredPopup->setItems and show");
    filteredPopup->setItems(items);
    filteredPopup->show(textEditor);
    DBG("filteredPopup->show completed, isVisible: " << (filteredPopup->isVisible() ? "true" : "false"));
}

void LibraryBrowser::buildFilteredList(std::vector<FilteredListPopup::Item>& items, const juce::String& searchText)
{
    itemIndices.clear();

    if (!libraryTree.isValid())
        return;

    auto lowerSearch = searchText.toLowerCase();

    // Iterate through library tree: library -> fileNodes -> categoryNodes -> itemNodes
    for (int fileIdx = 0; fileIdx < libraryTree.getNumChildren(); ++fileIdx)
    {
        auto fileNode = libraryTree.getChild(fileIdx);

        for (int categoryIdx = 0; categoryIdx < fileNode.getNumChildren(); ++categoryIdx)
        {
            auto categoryNode = fileNode.getChild(categoryIdx);
            auto categoryNameVar = categoryNode.getProperty("name");
            juce::String categoryName = categoryNameVar.toString();

            bool categoryHasMatches = false;
            for (int childIdx = 0; childIdx < categoryNode.getNumChildren(); ++childIdx)
            {
                auto childNode = categoryNode.getChild(childIdx);
                auto childLabelVar = childNode.getProperty("name");
                juce::String childLabel = childLabelVar.toString();

                if (childLabel.toLowerCase().contains(lowerSearch))
                {
                    categoryHasMatches = true;
                    break;
                }
            }

            if (categoryHasMatches)
            {
                // Add category header
                items.push_back({categoryName, categoryName, -1, true});

                // Add matching child nodes
                for (int childIdx = 0; childIdx < categoryNode.getNumChildren(); ++childIdx)
                {
                    auto childNode = categoryNode.getChild(childIdx);
                    auto childLabelVar = childNode.getProperty("name");
                    juce::String childLabel = childLabelVar.toString();

                    if (childLabel.toLowerCase().contains(lowerSearch))
                    {
                        int index = (int)itemIndices.size();
                        itemIndices.push_back({fileIdx, categoryIdx, childIdx});
                        items.push_back({categoryName, childLabel, index, false});
                    }
                }
            }
        }
    }
}

void LibraryBrowser::onFilteredItemSelected(int index)
{
    if (index < 0 || index >= (int)itemIndices.size() || !libraryTree.isValid() || !itemSelectedCallback)
        return;

    const auto& idx = itemIndices[index];

    auto fileNode = libraryTree.getChild(idx.fileIdx);
    if (!fileNode.isValid())
        return;

    auto categoryNode = fileNode.getChild(idx.categoryIdx);
    if (!categoryNode.isValid())
        return;

    auto childNode = categoryNode.getChild(idx.childIdx);
    if (!childNode.isValid())
        return;

    auto categoryVar = categoryNode.getProperty("name");
    auto labelVar = childNode.getProperty("name");
    auto itemDataVar = childNode.getProperty("data");

    if (categoryVar.isVoid() || labelVar.isVoid() || itemDataVar.isVoid())
        return;

    currentItemName = labelVar.toString();
    textEditor.setText(currentItemName, false);

    itemSelectedCallback(categoryVar.toString(), labelVar.toString(), itemDataVar.toString());

    textEditor.grabKeyboardFocus();
}

void LibraryBrowser::buildHierarchicalMenu(juce::PopupMenu& menu)
{
    itemIndices.clear();
    if (!libraryTree.isValid())
    {
        menu.addItem(-1, "No library data", false);
        return;
    }

    int itemId = 1;
    const int maxItemsPerPage = 80;

    // Build hierarchical menu from ValueTree structure: library -> fileNodes -> categoryNodes -> childNodes
    for (int fileIdx = 0; fileIdx < libraryTree.getNumChildren(); ++fileIdx)
    {
        auto fileNode = libraryTree.getChild(fileIdx);

        for (int categoryIdx = 0; categoryIdx < fileNode.getNumChildren(); ++categoryIdx)
        {
            auto categoryNode = fileNode.getChild(categoryIdx);
            auto categoryNameVar = categoryNode.getProperty("name");
            if (categoryNameVar.isVoid())
                continue;

            juce::String categoryName = categoryNameVar.toString();
            int numChildren = categoryNode.getNumChildren();
            if (numChildren == 0)
                continue;

            // If category has few children, create single submenu
            if (numChildren <= maxItemsPerPage)
            {
                juce::PopupMenu categoryMenu;
                for (int childIdx = 0; childIdx < numChildren; ++childIdx)
                {
                    auto childNode = categoryNode.getChild(childIdx);
                    auto childNameVar = childNode.getProperty("name");
                    if (childNameVar.isVoid())
                        continue;

                    itemIndices.push_back({fileIdx, categoryIdx, childIdx});
                    categoryMenu.addItem(itemId++, childNameVar.toString());
                }
                menu.addSubMenu(categoryName, categoryMenu);
            }
            else
            {
                // If category has many children, split into pages
                int numPages = (numChildren + maxItemsPerPage - 1) / maxItemsPerPage;
                for (int page = 0; page < numPages; ++page)
                {
                    juce::PopupMenu pageMenu;
                    int startIdx = page * maxItemsPerPage;
                    int endIdx = juce::jmin(startIdx + maxItemsPerPage, numChildren);

                    for (int childIdx = startIdx; childIdx < endIdx; ++childIdx)
                    {
                        auto childNode = categoryNode.getChild(childIdx);
                        auto childNameVar = childNode.getProperty("name");
                        if (childNameVar.isVoid())
                            continue;

                        itemIndices.push_back({fileIdx, categoryIdx, childIdx});
                        pageMenu.addItem(itemId++, childNameVar.toString());
                    }

                    juce::String pageName = categoryName + " " + juce::String(page + 1);
                    menu.addSubMenu(pageName, pageMenu);
                }
            }
        }
    }

    if (itemIndices.empty())
        menu.addItem(-1, "No items available", false);
}

void LibraryBrowser::onMenuResult(int result)
{
    if (result == 0 || !libraryTree.isValid() || !itemSelectedCallback)
        return;

    int index = result - 1;
    if (index < 0 || index >= static_cast<int>(itemIndices.size()))
        return;

    const auto& idx = itemIndices[index];

    auto fileNode = libraryTree.getChild(idx.fileIdx);
    if (!fileNode.isValid())
        return;

    auto categoryNode = fileNode.getChild(idx.categoryIdx);
    if (!categoryNode.isValid())
        return;

    auto childNode = categoryNode.getChild(idx.childIdx);
    if (!childNode.isValid())
        return;

    auto categoryVar = categoryNode.getProperty("name");
    auto labelVar = childNode.getProperty("name");
    auto itemDataVar = childNode.getProperty("data");

    if (categoryVar.isVoid() || labelVar.isVoid() || itemDataVar.isVoid())
        return;

    currentItemName = labelVar.toString();
    textEditor.setText(currentItemName, false);

    itemSelectedCallback(categoryVar.toString(), labelVar.toString(), itemDataVar.toString());
}

// WASD Navigation implementation
void LibraryBrowser::buildFlatItemList()
{
    flatItemList.clear();
    currentFlatIndex = -1;

    if (!libraryTree.isValid())
        return;

    // Build flat list of all items in order
    for (int fileIdx = 0; fileIdx < libraryTree.getNumChildren(); ++fileIdx)
    {
        auto fileNode = libraryTree.getChild(fileIdx);

        for (int categoryIdx = 0; categoryIdx < fileNode.getNumChildren(); ++categoryIdx)
        {
            auto categoryNode = fileNode.getChild(categoryIdx);
            int numChildren = categoryNode.getNumChildren();

            for (int childIdx = 0; childIdx < numChildren; ++childIdx)
                flatItemList.push_back({fileIdx, categoryIdx, childIdx});
        }
    }
}

void LibraryBrowser::navigateToFlatIndex(int index)
{
    if (flatItemList.empty())
        buildFlatItemList();

    if (flatItemList.empty())
        return;

    // Clamp index to valid range
    index = juce::jlimit(0, (int)flatItemList.size() - 1, index);
    currentFlatIndex = index;

    applyCurrentItem();
}

void LibraryBrowser::applyCurrentItem()
{
    if (currentFlatIndex < 0
        || currentFlatIndex >= (int)flatItemList.size()
        || !libraryTree.isValid()
        || !itemSelectedCallback)
        return;

    const auto& idx = flatItemList[currentFlatIndex];

    auto fileNode = libraryTree.getChild(idx.fileIdx);
    if (!fileNode.isValid())
        return;

    auto categoryNode = fileNode.getChild(idx.categoryIdx);
    if (!categoryNode.isValid())
        return;

    auto childNode = categoryNode.getChild(idx.childIdx);
    if (!childNode.isValid())
        return;

    auto categoryVar = categoryNode.getProperty("name");
    auto labelVar = childNode.getProperty("name");
    auto itemDataVar = childNode.getProperty("data");

    if (categoryVar.isVoid() || labelVar.isVoid() || itemDataVar.isVoid())
        return;

    currentItemName = labelVar.toString();
    textEditor.setText(currentItemName, false);

    itemSelectedCallback(categoryVar.toString(), labelVar.toString(), itemDataVar.toString());
}

void LibraryBrowser::updateWasdToggleState(bool enabled)
{
    if (enabled)
    {
        // If another instance is active, disable it first
        if (activeWasdInstance != nullptr && activeWasdInstance != this)
            activeWasdInstance->wasdToggleButton.setToggleState(false, juce::dontSendNotification);

        // Set this instance as the active one
        activeWasdInstance = this;
    }
    else
    {
        // If this instance is being disabled and it's the active one, clear the static pointer
        if (activeWasdInstance == this)
            activeWasdInstance = nullptr;
    }
}

bool LibraryBrowser::keyPressed(const juce::KeyPress& key)
{
    // Only handle WASD when toggle is enabled
    if (!wasdToggleButton.getToggleState())
        return false;

    // Build flat list on first use
    if (flatItemList.empty())
        buildFlatItemList();

    if (flatItemList.empty())
        return false;

    // If no current index, start at 0
    if (currentFlatIndex < 0)
        currentFlatIndex = 0;

    bool handled = false;

    if (key == juce::KeyPress('d') || key == juce::KeyPress('D'))
    {
        // D = Next preset
        navigateToFlatIndex(currentFlatIndex + 1);
        handled = true;
    }
    else if (key == juce::KeyPress('a') || key == juce::KeyPress('A'))
    {
        // A = Previous preset
        navigateToFlatIndex(currentFlatIndex - 1);
        handled = true;
    }
    else if (key == juce::KeyPress('s') || key == juce::KeyPress('S'))
    {
        // S = +10 steps forward
        navigateToFlatIndex(currentFlatIndex + 10);
        handled = true;
    }
    else if (key == juce::KeyPress('w') || key == juce::KeyPress('W'))
    {
        // W = -10 steps backward
        navigateToFlatIndex(currentFlatIndex - 10);
        handled = true;
    }

    return handled;
}
