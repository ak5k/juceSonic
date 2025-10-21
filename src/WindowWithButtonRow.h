#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ButtonRowComponent.h"
#include "JuceSonicLookAndFeel.h"

/**
 * @brief Base class for windows with a button row at the top
 *
 * Provides:
 * - A ButtonRowComponent at the top that resizes proportionally
 * - A status label at the bottom
 * - Automatic layout management
 * - Derived classes implement getMainComponent() to provide the central content
 */
class WindowWithButtonRow : public juce::Component
{
public:
    WindowWithButtonRow();
    ~WindowWithButtonRow() override;

    void resized() override;

    /**
     * @brief Set visibility of button row and status label
     */
    void setControlsVisible(bool visible);

    /**
     * @brief Set the menu title for narrow mode
     * When the window is too narrow, buttons are replaced with a single menu button
     */
    void setButtonMenuTitle(const juce::String& title)
    {
        buttonRow.setMenuTitle(title);
    }

protected:
    /**
     * @brief Get the main content component that fills the center
     * Derived classes must implement this to provide their main content area
     */
    virtual juce::Component* getMainComponent() = 0;

    /**
     * @brief Access to the button row for adding buttons
     */
    ButtonRowComponent& getButtonRow()
    {
        return buttonRow;
    }

    /**
     * @brief Access to the status label
     */
    juce::Label& getStatusLabel()
    {
        return statusLabel;
    }

    /**
     * @brief Cache the current selection for later use
     * Used to preserve selection when button clicks clear the tree selection
     */
    void cacheSelection(const juce::Array<juce::TreeViewItem*>& items)
    {
        cachedSelectedItems = items;
    }

    /**
     * @brief Get the cached selection
     */
    const juce::Array<juce::TreeViewItem*>& getCachedSelection() const
    {
        return cachedSelectedItems;
    }

    /**
     * @brief Clear the cached selection after operations complete
     */
    void clearCachedSelection()
    {
        cachedSelectedItems.clear();
    }

private:
    ButtonRowComponent buttonRow;
    juce::Label statusLabel;
    juce::SharedResourcePointer<SharedJuceSonicLookAndFeel> sharedLookAndFeel;

    // Cache selected items to preserve selection when button clicks clear tree selection
    juce::Array<juce::TreeViewItem*> cachedSelectedItems;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WindowWithButtonRow)
};
