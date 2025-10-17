#pragma once

#include "LibraryManager.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/**
 * @brief UI component for browsing and searching preset libraries
 *
 * A self-contained component with a label and ComboBox for browsing presets.
 * Displays presets in a hierarchical multi-column menu organized by library.
 * PluginEditor simply adds this component and sets its bounds.
 */
class LibraryBrowser : public juce::Component
{
public:
    /**
     * @brief Callback type for preset selection
     * @param libraryName The name of the library containing the preset
     * @param presetName The name of the selected preset
     * @param presetData The base64-encoded preset data
     */
    using PresetSelectedCallback = std::function<
        void(const juce::String& libraryName, const juce::String& presetName, const juce::String& presetData)>;

    LibraryBrowser();
    ~LibraryBrowser() override;

    /**
     * @brief Set the library manager to browse
     */
    void setLibraryManager(LibraryManager* manager);

    /**
     * @brief Set which sub-library to browse (e.g., "Presets")
     */
    void setSubLibraryName(const juce::String& name);

    /**
     * @brief Set callback for when user selects a preset
     */
    void setPresetSelectedCallback(PresetSelectedCallback callback);

    /**
     * @brief Rebuild the preset menu from current library data
     */
    void updatePresetList();

    /**
     * @brief Set the text displayed on the label
     */
    void setLabelText(const juce::String& text);

    /**
     * @brief Get the ComboBox for direct access if needed
     */
    juce::ComboBox& getComboBox()
    {
        return comboBox;
    }

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // Custom LookAndFeel for multi-column menu
    class BrowserLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override;
    };

    // Mouse listener to handle dropdown arrow clicks
    class BrowserMouseListener : public juce::MouseListener
    {
    public:
        explicit BrowserMouseListener(LibraryBrowser* owner);
        void mouseDown(const juce::MouseEvent& event) override;

    private:
        LibraryBrowser* owner;
    };

    void buildHierarchicalMenu();
    void onPresetSelected();

    LibraryManager* libraryManager = nullptr;
    juce::String subLibraryName = "Presets"; // Default sub-library name
    PresetSelectedCallback presetSelectedCallback;

    juce::ComboBox comboBox;
    juce::Label label;
    BrowserLookAndFeel lookAndFeel;
    std::unique_ptr<BrowserMouseListener> mouseListener;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryBrowser)
};
