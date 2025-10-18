#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Custom LookAndFeel with dark theme
class JuceSonicLookAndFeel : public juce::LookAndFeel_V4
{
public:
    JuceSonicLookAndFeel()
    {
        // Base dark background color
        auto baseBackground = juce::Colour(0xff010409);

        // Derived colors
        auto lighter = baseBackground.brighter(0.1f);
        auto lightest = baseBackground.brighter(0.2f);
        auto textColor = juce::Colours::white.withAlpha(0.9f);

        setColour(juce::ResizableWindow::backgroundColourId, baseBackground);
        setColour(juce::DocumentWindow::backgroundColourId, baseBackground);
        setColour(juce::TextButton::buttonColourId, lighter);
        setColour(juce::TextButton::textColourOffId, textColor);
        setColour(juce::ComboBox::backgroundColourId, lighter);
        setColour(juce::ComboBox::textColourId, textColor);
        setColour(juce::ComboBox::outlineColourId, lightest);
        setColour(juce::TextEditor::backgroundColourId, lighter);
        setColour(juce::TextEditor::textColourId, textColor);
        setColour(juce::TextEditor::outlineColourId, lightest);
        setColour(juce::Label::textColourId, textColor);
        setColour(juce::ListBox::backgroundColourId, baseBackground);
        setColour(juce::ListBox::outlineColourId, lightest);
        setColour(juce::Slider::backgroundColourId, lighter);
        setColour(juce::Slider::thumbColourId, lightest);
        setColour(juce::Slider::trackColourId, lightest);
        setColour(juce::Slider::textBoxTextColourId, textColor);
        setColour(juce::Slider::textBoxBackgroundColourId, lighter);
        setColour(juce::Slider::textBoxOutlineColourId, lightest);
        setColour(juce::TreeView::backgroundColourId, baseBackground);
        setColour(juce::PopupMenu::backgroundColourId, lighter);
        setColour(juce::PopupMenu::textColourId, textColor);
        setColour(juce::PopupMenu::headerTextColourId, textColor);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, lightest);
        setColour(juce::PopupMenu::highlightedTextColourId, textColor);

        // Additional multicolumn menu colors
        setColour(juce::TooltipWindow::backgroundColourId, lighter);
        setColour(juce::TooltipWindow::textColourId, textColor);
        setColour(juce::TooltipWindow::outlineColourId, lightest);

        // AlertWindow colors
        setColour(juce::AlertWindow::backgroundColourId, baseBackground);
        setColour(juce::AlertWindow::textColourId, textColor);
        setColour(juce::AlertWindow::outlineColourId, lightest);

        // Set this as the default LookAndFeel
        juce::LookAndFeel::setDefaultLookAndFeel(this);
    }

    // Override scrollbar width to make handles thicker
    int getDefaultScrollbarWidth() override
    {
        return 16; // Default is 12, so 16 is 33% thicker
    }
};

// Shared LookAndFeel wrapper for use with SharedResourcePointer
struct SharedJuceSonicLookAndFeel
{
    JuceSonicLookAndFeel lf;
};
