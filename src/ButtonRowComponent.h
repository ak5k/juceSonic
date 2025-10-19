#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>
#include <functional>

/**
 * @brief Base component for a horizontal row of equally-sized buttons
 *
 * Features:
 * - All buttons are always visible with equal widths
 * - Buttons resize proportionally with the component
 * - Button height is proportional to component height
 * - Simple interface to add buttons with name and callback
 */
class ButtonRowComponent : public juce::Component
{
public:
    ButtonRowComponent() = default;
    ~ButtonRowComponent() override = default;

    /**
     * @brief Add a button to the row
     * @param name Button label text
     * @param callback Function to call when button is clicked
     * @return Reference to the created button for further customization
     */
    juce::TextButton& addButton(const juce::String& name, std::function<void()> callback)
    {
        auto button = std::make_unique<juce::TextButton>(name);
        button->onClick = std::move(callback);
        addAndMakeVisible(*button);

        buttons.push_back(std::move(button));
        resized();

        return *buttons.back();
    }

    /**
     * @brief Get button by index
     */
    juce::TextButton* getButton(int index)
    {
        if (index >= 0 && index < static_cast<int>(buttons.size()))
            return buttons[index].get();
        return nullptr;
    }

    /**
     * @brief Get number of buttons in the row
     */
    int getButtonCount() const
    {
        return static_cast<int>(buttons.size());
    }

    /**
     * @brief Clear all buttons
     */
    void clearButtons()
    {
        buttons.clear();
        resized();
    }

    void resized() override
    {
        if (buttons.empty())
            return;

        auto bounds = getLocalBounds();
        const int spacing = 4;
        const int totalSpacing = spacing * (static_cast<int>(buttons.size()) - 1);
        const int availableWidth = bounds.getWidth() - totalSpacing;
        const int buttonWidth = availableWidth / static_cast<int>(buttons.size());

        for (size_t i = 0; i < buttons.size(); ++i)
        {
            if (i > 0)
                bounds.removeFromLeft(spacing);

            buttons[i]->setBounds(bounds.removeFromLeft(buttonWidth));
        }
    }

private:
    std::vector<std::unique_ptr<juce::TextButton>> buttons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ButtonRowComponent)
};
