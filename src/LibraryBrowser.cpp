#include "LibraryBrowser.h"

//==============================================================================
// BrowserLookAndFeel implementation
juce::PopupMenu::Options
LibraryBrowser::BrowserLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label)
{
    auto opts = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label);
    return opts.withMaximumNumColumns(4);
}

//==============================================================================
// BrowserMouseListener implementation
LibraryBrowser::BrowserMouseListener::BrowserMouseListener(LibraryBrowser* ownerIn)
    : owner(ownerIn)
{
}

void LibraryBrowser::BrowserMouseListener::mouseDown(const juce::MouseEvent& event)
{
    if (!owner)
        return;

    auto& combo = owner->comboBox;
    auto arrowBounds = combo.getLocalBounds().removeFromRight(combo.getHeight());
    auto localPos = combo.getLocalPoint(&combo, event.getPosition());

    if (arrowBounds.contains(localPos))
    {
        // User clicked arrow - show popup
        // Only rebuild menu if cache is invalid (library changed)
        if (!owner->menuCacheValid)
        {
            DBG("LibraryBrowser: Menu cache invalid, rebuilding...");
            owner->buildHierarchicalMenu();
            owner->menuCacheValid = true;
        }
        else
        {
            DBG("LibraryBrowser: Using cached menu");
        }

        combo.hidePopup();
        combo.showPopup();
    }
}

//==============================================================================
// LibraryBrowser implementation
LibraryBrowser::LibraryBrowser()
{
    addAndMakeVisible(label);
    label.setText("Presets:", juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(comboBox);
    comboBox.setTextWhenNothingSelected("(No preset loaded)");
    comboBox.setTextWhenNoChoicesAvailable("No presets available");
    comboBox.setLookAndFeel(&lookAndFeel);
    comboBox.onChange = [this]() { onPresetSelected(); };

    mouseListener = std::make_unique<BrowserMouseListener>(this);
    comboBox.addMouseListener(mouseListener.get(), false);
}

LibraryBrowser::~LibraryBrowser()
{
    comboBox.setLookAndFeel(nullptr);
}

void LibraryBrowser::setLibraryManager(LibraryManager* manager)
{
    libraryManager = manager;
    menuCacheValid = false; // Invalidate cache when manager changes
    updatePresetList();
}

void LibraryBrowser::setSubLibraryName(const juce::String& name)
{
    subLibraryName = name;
    menuCacheValid = false; // Invalidate cache when library name changes
    updatePresetList();
}

void LibraryBrowser::setPresetSelectedCallback(PresetSelectedCallback callback)
{
    presetSelectedCallback = std::move(callback);
}

void LibraryBrowser::setLabelText(const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
}

void LibraryBrowser::updatePresetList()
{
    DBG("LibraryBrowser::updatePresetList called - invalidating cache");
    menuCacheValid = false; // Invalidate cache when library data changes
    buildHierarchicalMenu();
    menuCacheValid = true; // Mark as valid after rebuild
}

void LibraryBrowser::buildHierarchicalMenu()
{
    DBG("LibraryBrowser::buildHierarchicalMenu - Starting");
    comboBox.clear();
    presetIndices.clear();      // Clear the index
    presetIndices.reserve(500); // Reserve space for typical preset count

    if (!libraryManager)
    {
        DBG("  No library manager!");
        comboBox.setEnabled(false);
        return;
    }

    // Get the library we're browsing (e.g., "Presets")
    // This returns a reference to the ValueTree, not a copy
    auto library = libraryManager->getLibrary(subLibraryName);
    if (!library.isValid())
    {
        DBG("  Library '" << subLibraryName << "' not found!");
        comboBox.setEnabled(false);
        return;
    }

    DBG("  Library '" << subLibraryName << "' has " << library.getNumChildren() << " children");

    int itemId = 1;
    const int maxPresetsPerPage = 80;

    // Structure: Library > PresetFile > PresetBank > Preset
    // We want to show: PresetBank as submenu > Presets

    // Iterate through PresetFiles
    int numFiles = library.getNumChildren();
    for (int fileIdx = 0; fileIdx < numFiles; ++fileIdx)
    {
        auto presetFile = library.getChild(fileIdx);
        DBG("  Processing PresetFile " << fileIdx << ", type: " << presetFile.getType().toString());

        // Iterate through banks in this file
        int numBanks = presetFile.getNumChildren();
        DBG("    Has " << numBanks << " banks");

        for (int bankIdx = 0; bankIdx < numBanks; ++bankIdx)
        {
            auto bank = presetFile.getChild(bankIdx);
            DBG("    Processing Bank " << bankIdx << ", type: " << bank.getType().toString());

            // Read bank name directly from property (no string copy until necessary)
            auto bankNameVar = bank.getProperty("name");
            if (bankNameVar.isVoid())
                continue;

            juce::String bankName = bankNameVar.toString();
            DBG("      Bank name: " << bankName);

            int numPresets = bank.getNumChildren();
            DBG("      Num presets: " << numPresets);

            if (numPresets == 0)
                continue;

            if (numPresets <= maxPresetsPerPage)
            {
                // Single page - create submenu with all presets
                juce::PopupMenu bankMenu;

                for (int presetIdx = 0; presetIdx < numPresets; ++presetIdx)
                {
                    auto preset = bank.getChild(presetIdx);

                    // Read preset name directly (no copy until menu creation)
                    auto presetNameVar = preset.getProperty("name");
                    if (presetNameVar.isVoid())
                        continue;

                    // Store index for fast lookup later
                    presetIndices.push_back({fileIdx, bankIdx, presetIdx});

                    bankMenu.addItem(itemId++, presetNameVar.toString());
                }

                comboBox.getRootMenu()->addSubMenu(bankName, bankMenu);
            }
            else
            {
                // Multiple pages needed - split into chunks
                int numPages = (numPresets + maxPresetsPerPage - 1) / maxPresetsPerPage;

                for (int page = 0; page < numPages; ++page)
                {
                    juce::PopupMenu pageMenu;
                    int startIdx = page * maxPresetsPerPage;
                    int endIdx = juce::jmin(startIdx + maxPresetsPerPage, numPresets);

                    for (int presetIdx = startIdx; presetIdx < endIdx; ++presetIdx)
                    {
                        auto preset = bank.getChild(presetIdx);

                        // Read preset name directly
                        auto presetNameVar = preset.getProperty("name");
                        if (presetNameVar.isVoid())
                            continue;

                        // Store index for fast lookup later
                        presetIndices.push_back({fileIdx, bankIdx, presetIdx});

                        pageMenu.addItem(itemId++, presetNameVar.toString());
                    }

                    juce::String pageName = bankName + " " + juce::String(page + 1);
                    comboBox.getRootMenu()->addSubMenu(pageName, pageMenu);
                }
            }
        }
    }

    DBG("  Total items indexed: " << presetIndices.size());
    DBG("  Enabling comboBox: " << (itemId > 1 ? "YES" : "NO"));

    comboBox.setEnabled(itemId > 1); // Enable if we have presets
}

void LibraryBrowser::onPresetSelected()
{
    int selectedId = comboBox.getSelectedId();
    if (selectedId == 0 || !libraryManager || !presetSelectedCallback)
        return;

    // Convert menu item ID to array index (IDs start at 1)
    int index = selectedId - 1;

    // Bounds check
    if (index < 0 || index >= static_cast<int>(presetIndices.size()))
    {
        DBG("LibraryBrowser::onPresetSelected - Invalid index: " << index);
        return;
    }

    // Direct lookup using pre-built index (O(1) instead of O(n) tree traversal)
    const auto& idx = presetIndices[index];

    // Get the library (returns reference, no copy)
    auto library = libraryManager->getLibrary(subLibraryName);
    if (!library.isValid())
        return;

    // Navigate directly to the preset using cached indices
    auto presetFile = library.getChild(idx.fileIdx);
    if (!presetFile.isValid())
        return;

    auto bank = presetFile.getChild(idx.bankIdx);
    if (!bank.isValid())
        return;

    auto preset = bank.getChild(idx.presetIdx);
    if (!preset.isValid())
        return;

    // Read properties directly (minimal string copies)
    auto libraryNameVar = bank.getProperty("name");
    auto presetNameVar = preset.getProperty("name");
    auto presetDataVar = preset.getProperty("data");

    if (libraryNameVar.isVoid() || presetNameVar.isVoid() || presetDataVar.isVoid())
    {
        DBG("LibraryBrowser::onPresetSelected - Missing required properties");
        return;
    }

    // Only convert to strings when calling the callback
    presetSelectedCallback(libraryNameVar.toString(), presetNameVar.toString(), presetDataVar.toString());
}

void LibraryBrowser::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void LibraryBrowser::resized()
{
    auto area = getLocalBounds();

    // Label on the left (fixed width)
    const int labelWidth = 60;
    label.setBounds(area.removeFromLeft(labelWidth));

    // Small gap
    area.removeFromLeft(5);

    // ComboBox takes the rest
    comboBox.setBounds(area);
}
