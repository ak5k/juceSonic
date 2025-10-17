#include "LibraryBrowser.h"

//==============================================================================
juce::PopupMenu::Options
LibraryBrowser::BrowserLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label)
{
    auto opts = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label);
    return opts.withMaximumNumColumns(4);
}

//==============================================================================
LibraryBrowser::LibraryBrowser()
{
    addAndMakeVisible(label);
    label.setText("", juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(textEditor);
    textEditor.setTextToShowWhenEmpty("", getLookAndFeel().findColour(juce::TextEditor::textColourId).withAlpha(0.5f));
    textEditor.onTextChange = [this]() { onSearchTextChanged(); };
    textEditor.onReturnKey = [this]()
    {
        auto text = textEditor.getText();
        if (text.length() >= 3)
            showFilteredPopup(text);
        else if (text.isEmpty())
            showHierarchicalPopup();
    };
    textEditor.onEscapeKey = [this]() { unfocusAllComponents(); };
    textEditor.onFocusLost = [this]() {};

    addAndMakeVisible(dropdownButton);
    dropdownButton.setButtonText("v");
    dropdownButton.onClick = [this]() { showHierarchicalPopup(); };
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
    juce::PopupMenu menu;
    buildFilteredMenu(menu, searchText);

    if (itemIndices.empty())
        return;

    menu.setLookAndFeel(&lookAndFeel);

    auto options = juce::PopupMenu::Options()
                       .withTargetComponent(&textEditor)
                       .withMinimumWidth(textEditor.getWidth() + dropdownButton.getWidth())
                       .withMaximumNumColumns(1);

    menu.showMenuAsync(
        options,
        [this](int result)
        {
            if (result > 0)
                onMenuResult(result);
        }
    );
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

void LibraryBrowser::buildFilteredMenu(juce::PopupMenu& menu, const juce::String& searchText)
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
    int menuItemId = 1;
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
                // Add bank name as a disabled item (using negative ID) to keep alignment
                menu.addItem(-1, bankName, false);

                for (int itemIdx = 0; itemIdx < bank.getNumChildren(); ++itemIdx)
                {
                    auto item = bank.getChild(itemIdx);
                    auto itemNameVar = item.getProperty("name");
                    juce::String itemName = itemNameVar.toString();
                    if (itemName.toLowerCase().contains(lowerSearch))
                    {
                        menu.addItem(menuItemId, itemName);
                        itemIndices.push_back({fileIdx, bankIdx, itemIdx});
                        menuItemId++;
                    }
                }
            }
        }
    }
    if (itemIndices.empty())
        menu.addItem(-1, "(No matching items)", false);
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
