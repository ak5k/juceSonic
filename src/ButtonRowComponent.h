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
 * - At very narrow widths, shows a single menu button that opens a popup menu
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
        button->onClick = callback;
        addAndMakeVisible(*button);

        ButtonInfo info;
        info.name = name;
        info.callback = callback;
        buttonInfos.push_back(info);

        buttons.push_back(std::move(button));
        resized();

        return *buttons.back();
    }

    /**
     * @brief Set the menu title to display when in narrow mode
     */
    void setMenuTitle(const juce::String& title)
    {
        menuTitle = title;
        if (menuButton != nullptr)
            menuButton->setButtonText(title);
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
        buttonInfos.clear();
        resized();
    }

    void resized() override
    {
        if (buttons.empty())
            return;

        auto bounds = getLocalBounds();

        // Calculate minimum width needed to display at least one character per button
        // Estimate: 10px per character + button padding/borders (~20px per button)
        const int minCharWidth = 10;
        const int buttonPadding = 20;
        const int minButtonWidth = minCharWidth + buttonPadding;
        const int spacing = 4;
        const int totalSpacing = spacing * (static_cast<int>(buttons.size()) - 1);
        const int minTotalWidth = minButtonWidth * static_cast<int>(buttons.size()) + totalSpacing;

        // Check if we need to switch to menu mode
        if (bounds.getWidth() < minTotalWidth)
        {
            // Hide all regular buttons
            for (auto& button : buttons)
                button->setVisible(false);

            // Show menu button
            if (menuButton == nullptr)
            {
                menuButton = std::make_unique<juce::TextButton>(menuTitle.isEmpty() ? "Menu" : menuTitle);
                menuButton->onClick = [this]() { showMenu(); };
                addAndMakeVisible(*menuButton);
            }
            menuButton->setVisible(true);
            menuButton->setBounds(bounds);
        }
        else
        {
            // Hide menu button
            if (menuButton != nullptr)
                menuButton->setVisible(false);

            // Show all regular buttons with equal widths
            const int availableWidth = bounds.getWidth() - totalSpacing;
            const int buttonWidth = availableWidth / static_cast<int>(buttons.size());

            for (size_t i = 0; i < buttons.size(); ++i)
            {
                if (i > 0)
                    bounds.removeFromLeft(spacing);

                buttons[i]->setVisible(true);
                buttons[i]->setBounds(bounds.removeFromLeft(buttonWidth));
            }
        }
    }

private:
    struct ButtonInfo
    {
        juce::String name;
        std::function<void()> callback;
    };

    void showMenu()
    {
        juce::PopupMenu menu;

        for (size_t i = 0; i < buttonInfos.size(); ++i)
        {
            const auto& info = buttonInfos[i];

            // Check if the button is enabled
            bool isEnabled = buttons[i] && buttons[i]->isEnabled();

            menu.addItem(static_cast<int>(i) + 1, info.name, isEnabled);
        }

        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(menuButton.get()),
            [this](int result)
            {
                if (result > 0 && result <= static_cast<int>(buttonInfos.size()))
                {
                    const auto& info = buttonInfos[result - 1];
                    if (info.callback)
                        info.callback();
                }
            }
        );
    }

    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    std::vector<ButtonInfo> buttonInfos;
    std::unique_ptr<juce::TextButton> menuButton;
    juce::String menuTitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ButtonRowComponent)
};
