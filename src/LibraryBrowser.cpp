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
        // User clicked arrow - show hierarchical menu
        combo.hidePopup();
        owner->buildHierarchicalMenu();
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
    updatePresetList();
}

void LibraryBrowser::setSubLibraryName(const juce::String& name)
{
    subLibraryName = name;
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
    DBG("LibraryBrowser::updatePresetList called");
    buildHierarchicalMenu();
}

void LibraryBrowser::buildHierarchicalMenu()
{
    DBG("LibraryBrowser::buildHierarchicalMenu - Starting");
    comboBox.clear();

    if (!libraryManager)
    {
        DBG("  No library manager!");
        comboBox.setEnabled(false);
        return;
    }

    // Get the sub-library we're browsing (e.g., "Presets")
    auto subLibrary = libraryManager->getSubLibrary(subLibraryName);
    if (!subLibrary.isValid())
    {
        DBG("  Sub-library '" << subLibraryName << "' not found!");
        comboBox.setEnabled(false);
        return;
    }

    DBG("  Sub-library '" << subLibraryName << "' has " << subLibrary.getNumChildren() << " children");

    int itemId = 1;
    const int maxPresetsPerPage = 80;

    // Structure: SubLibrary > PresetFile > PresetBank > Preset
    // We want to show: PresetBank as submenu > Presets

    // Iterate through PresetFiles
    for (int fileIdx = 0; fileIdx < subLibrary.getNumChildren(); ++fileIdx)
    {
        auto presetFile = subLibrary.getChild(fileIdx);
        DBG("  Processing PresetFile " << fileIdx << ", type: " << presetFile.getType().toString());
        DBG("    Has " << presetFile.getNumChildren() << " children");

        // Iterate through banks in this file
        for (int bankIdx = 0; bankIdx < presetFile.getNumChildren(); ++bankIdx)
        {
            auto bank = presetFile.getChild(bankIdx);
            DBG("    Processing Bank " << bankIdx << ", type: " << bank.getType().toString());

            juce::String bankName = bank.getProperty("name").toString();
            DBG("      Bank name: " << bankName);

            int numPresets = bank.getNumChildren();
            DBG("      Num presets: " << numPresets);

            if (numPresets == 0)
                continue;

            if (numPresets <= maxPresetsPerPage)
            {
                // Single page - create submenu with all presets
                juce::PopupMenu bankMenu;
                for (int i = 0; i < numPresets; ++i)
                {
                    auto preset = bank.getChild(i);
                    juce::String presetName = preset.getProperty("name").toString();
                    bankMenu.addItem(itemId++, presetName);
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

                    for (int i = startIdx; i < endIdx; ++i)
                    {
                        auto preset = bank.getChild(i);
                        juce::String presetName = preset.getProperty("name").toString();
                        pageMenu.addItem(itemId++, presetName);
                    }

                    juce::String pageName = bankName + " " + juce::String(page + 1);
                    comboBox.getRootMenu()->addSubMenu(pageName, pageMenu);
                }
            }
        }
    }

    DBG("  Total items added: " << (itemId - 1));
    DBG("  Enabling comboBox: " << (itemId > 1 ? "YES" : "NO"));

    comboBox.setEnabled(itemId > 1); // Enable if we have presets
}

void LibraryBrowser::onPresetSelected()
{
    int selectedId = comboBox.getSelectedId();
    if (selectedId == 0 || !libraryManager || !presetSelectedCallback)
        return;

    // Find the preset by ID
    // Structure: SubLibrary > PresetFile > PresetBank > Preset
    auto subLibrary = libraryManager->getSubLibrary(subLibraryName);
    if (!subLibrary.isValid())
        return;

    int currentId = 1;

    // Iterate through PresetFiles
    for (int fileIdx = 0; fileIdx < subLibrary.getNumChildren(); ++fileIdx)
    {
        auto presetFile = subLibrary.getChild(fileIdx);

        // Iterate through banks in this file
        for (int bankIdx = 0; bankIdx < presetFile.getNumChildren(); ++bankIdx)
        {
            auto bank = presetFile.getChild(bankIdx);
            juce::String libraryName = bank.getProperty("name").toString();

            // Iterate through presets in this bank
            for (int presetIdx = 0; presetIdx < bank.getNumChildren(); ++presetIdx)
            {
                if (currentId == selectedId)
                {
                    auto preset = bank.getChild(presetIdx);
                    juce::String presetName = preset.getProperty("name").toString();
                    juce::String presetData = preset.getProperty("data").toString();

                    presetSelectedCallback(libraryName, presetName, presetData);
                    return;
                }
                currentId++;
            }
        }
    }
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
