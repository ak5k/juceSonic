#include "LibraryBrowser.h"

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

        // Get screen position of textEditor
        auto screenBounds = attachTo.getScreenBounds();

        // Convert to top-level component coordinates
        auto topLevelBounds = topLevel->getLocalArea(nullptr, screenBounds);

        int height = juce::jmin((int)itemList.size() * itemHeight, 400);
        DBG("Setting bounds: x="
            << topLevelBounds.getX()
            << ", y="
            << topLevelBounds.getBottom()
            << ", width="
            << topLevelBounds.getWidth()
            << ", height="
            << height);
        setBounds(topLevelBounds.getX(), topLevelBounds.getBottom(), topLevelBounds.getWidth(), height);

        DBG("Calling toFront (without grabbing focus)");
        toFront(false); // Don't grab focus - let text editor keep it
        repaint();
        DBG("FilteredListPopup::show completed");
    }
}

void LibraryBrowser::FilteredListPopup::hide()
{
    if (auto* parent = getParentComponent())
        parent->removeChildComponent(this);
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
        int itemTop = selectedIndex * itemHeight - scrollOffset;
        int itemBottom = itemTop + itemHeight;

        if (itemBottom > getHeight())
        {
            // Scroll down to show item
            scrollOffset += (itemBottom - getHeight());
            repaint();
        }
        else if (itemTop < 0)
        {
            // Scroll up to show item
            scrollOffset += itemTop;
            repaint();
        }
    }
}

void LibraryBrowser::FilteredListPopup::paint(juce::Graphics& g)
{
    auto& lf = getLookAndFeel();

    // Draw background
    g.fillAll(lf.findColour(juce::PopupMenu::backgroundColourId));

    // Draw border
    g.setColour(lf.findColour(juce::PopupMenu::backgroundColourId).contrasting(0.5f));
    g.drawRect(getLocalBounds(), 1);

    // Calculate visible item range
    int y = -scrollOffset;

    for (int i = 0; i < (int)itemList.size(); ++i)
    {
        auto itemBounds = juce::Rectangle<int>(0, y, getWidth(), itemHeight);

        // Only draw items that are visible
        if (itemBounds.getBottom() > 0 && itemBounds.getY() < getHeight())
        {
            if (itemList[i].isHeader)
            {
                // Draw section header
                g.setColour(lf.findColour(juce::PopupMenu::headerTextColourId));
                g.setFont(juce::FontOptions((float)itemHeight * 0.65f, juce::Font::bold));
                g.drawText(itemList[i].itemName, itemBounds.reduced(10, 0), juce::Justification::centredLeft);
            }
            else
            {
                // Draw regular item
                bool isHighlighted = (i == selectedIndex || i == hoveredIndex);

                if (isHighlighted)
                {
                    g.setColour(lf.findColour(juce::PopupMenu::highlightedBackgroundColourId));
                    g.fillRect(itemBounds);
                }

                g.setColour(
                    isHighlighted ? lf.findColour(juce::PopupMenu::highlightedTextColourId)
                                  : lf.findColour(juce::PopupMenu::textColourId)
                );

                g.setFont(juce::FontOptions((float)itemHeight * 0.7f));
                g.drawText(itemList[i].itemName, itemBounds.reduced(10, 0), juce::Justification::centredLeft);
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
    int index = (e.y + scrollOffset) / itemHeight;
    if (index >= 0 && index < (int)itemList.size() && !itemList[index].isHeader)
    {
        selectedIndex = index;
        selectCurrent();
    }
}

void LibraryBrowser::FilteredListPopup::mouseMove(const juce::MouseEvent& e)
{
    int index = (e.y + scrollOffset) / itemHeight;
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

    filteredPopup = std::make_unique<FilteredListPopup>(*this);
}

LibraryBrowser::~LibraryBrowser()
{
}

void LibraryBrowser::setLibraryManager(LibraryManager* manager)
{
    libraryManager = manager;
}

void LibraryBrowser::setLibraryName(const juce::String& name)
{
    libraryName = name;
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

    // Label on the left
    label.setBounds(area.removeFromLeft(labelWidth));
    area.removeFromLeft(spacing);

    // Dropdown button on the right of remaining area
    dropdownButton.setBounds(area.removeFromRight(buttonWidth));

    // Text editor fills the remaining space (aligned with dropdown)
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

    if (!libraryManager)
        return;

    auto library = libraryManager->getLibrary(libraryName);
    if (!library.isValid())
        return;

    auto lowerSearch = searchText.toLowerCase();

    for (int fileIdx = 0; fileIdx < library.getNumChildren(); ++fileIdx)
    {
        auto file = library.getChild(fileIdx);

        for (int bankIdx = 0; bankIdx < file.getNumChildren(); ++bankIdx)
        {
            auto bank = file.getChild(bankIdx);
            auto bankNameVar = bank.getProperty("name");
            juce::String bankName = bankNameVar.toString();

            bool bankHasMatches = false;
            for (int itemIdx = 0; itemIdx < bank.getNumChildren(); ++itemIdx)
            {
                auto item = bank.getChild(itemIdx);
                auto itemNameVar = item.getProperty("name");
                juce::String itemName = itemNameVar.toString();

                if (itemName.toLowerCase().contains(lowerSearch))
                {
                    bankHasMatches = true;
                    break;
                }
            }

            if (bankHasMatches)
            {
                // Add bank header
                items.push_back({bankName, bankName, -1, true});

                // Add matching items
                for (int itemIdx = 0; itemIdx < bank.getNumChildren(); ++itemIdx)
                {
                    auto item = bank.getChild(itemIdx);
                    auto itemNameVar = item.getProperty("name");
                    juce::String itemName = itemNameVar.toString();

                    if (itemName.toLowerCase().contains(lowerSearch))
                    {
                        int index = (int)itemIndices.size();
                        itemIndices.push_back({fileIdx, bankIdx, itemIdx});
                        items.push_back({bankName, itemName, index, false});
                    }
                }
            }
        }
    }
}

void LibraryBrowser::onFilteredItemSelected(int index)
{
    if (index < 0 || index >= (int)itemIndices.size() || !libraryManager || !itemSelectedCallback)
        return;

    const auto& idx = itemIndices[index];

    auto library = libraryManager->getLibrary(libraryName);
    if (!library.isValid())
        return;

    auto file = library.getChild(idx.fileIdx);
    if (!file.isValid())
        return;

    auto bank = file.getChild(idx.bankIdx);
    if (!bank.isValid())
        return;

    auto item = bank.getChild(idx.itemIdx);
    if (!item.isValid())
        return;

    auto bankNameVar = bank.getProperty("name");
    auto itemNameVar = item.getProperty("name");
    auto itemDataVar = item.getProperty("data");

    if (bankNameVar.isVoid() || itemNameVar.isVoid() || itemDataVar.isVoid())
        return;

    currentItemName = itemNameVar.toString();
    textEditor.setText(currentItemName, false);

    itemSelectedCallback(bankNameVar.toString(), itemNameVar.toString(), itemDataVar.toString());

    textEditor.grabKeyboardFocus();
}

void LibraryBrowser::buildHierarchicalMenu(juce::PopupMenu& menu)
{
    itemIndices.clear();
    if (!libraryManager)
    {
        menu.addItem(-1, "No library manager", false);
        return;
    }
    auto library = libraryManager->getLibrary(libraryName);
    if (!library.isValid())
    {
        menu.addItem(-1, "Invalid library", false);
        return;
    }
    int itemId = 1;
    const int maxItemsPerPage = 80;
    for (int fileIdx = 0; fileIdx < library.getNumChildren(); ++fileIdx)
    {
        auto file = library.getChild(fileIdx);
        for (int bankIdx = 0; bankIdx < file.getNumChildren(); ++bankIdx)
        {
            auto bank = file.getChild(bankIdx);
            auto bankNameVar = bank.getProperty("name");
            if (bankNameVar.isVoid())
                continue;
            juce::String bankName = bankNameVar.toString();
            int numItems = bank.getNumChildren();
            if (numItems == 0)
                continue;
            if (numItems <= maxItemsPerPage)
            {
                juce::PopupMenu bankMenu;
                for (int itemIdx = 0; itemIdx < numItems; ++itemIdx)
                {
                    auto item = bank.getChild(itemIdx);
                    auto itemNameVar = item.getProperty("name");
                    if (itemNameVar.isVoid())
                        continue;
                    itemIndices.push_back({fileIdx, bankIdx, itemIdx});
                    bankMenu.addItem(itemId++, itemNameVar.toString());
                }
                menu.addSubMenu(bankName, bankMenu);
            }
            else
            {
                int numPages = (numItems + maxItemsPerPage - 1) / maxItemsPerPage;
                for (int page = 0; page < numPages; ++page)
                {
                    juce::PopupMenu pageMenu;
                    int startIdx = page * maxItemsPerPage;
                    int endIdx = juce::jmin(startIdx + maxItemsPerPage, numItems);
                    for (int itemIdx = startIdx; itemIdx < endIdx; ++itemIdx)
                    {
                        auto item = bank.getChild(itemIdx);
                        auto itemNameVar = item.getProperty("name");
                        if (itemNameVar.isVoid())
                            continue;
                        itemIndices.push_back({fileIdx, bankIdx, itemIdx});
                        pageMenu.addItem(itemId++, itemNameVar.toString());
                    }
                    juce::String pageName = bankName + " " + juce::String(page + 1);
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
    if (result == 0 || !libraryManager || !itemSelectedCallback)
        return;
    int index = result - 1;
    if (index < 0 || index >= static_cast<int>(itemIndices.size()))
        return;
    const auto& idx = itemIndices[index];
    auto library = libraryManager->getLibrary(libraryName);
    if (!library.isValid())
        return;
    auto file = library.getChild(idx.fileIdx);
    if (!file.isValid())
        return;
    auto bank = file.getChild(idx.bankIdx);
    if (!bank.isValid())
        return;
    auto item = bank.getChild(idx.itemIdx);
    if (!item.isValid())
        return;
    auto bankNameVar = bank.getProperty("name");
    auto itemNameVar = item.getProperty("name");
    auto itemDataVar = item.getProperty("data");
    if (bankNameVar.isVoid() || itemNameVar.isVoid() || itemDataVar.isVoid())
        return;
    currentItemName = itemNameVar.toString();
    textEditor.setText(currentItemName, false);
    itemSelectedCallback(bankNameVar.toString(), itemNameVar.toString(), itemDataVar.toString());
}
