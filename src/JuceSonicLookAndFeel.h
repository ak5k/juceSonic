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

        // Title bar button colors
        myCustomColour = baseBackground.darker(0.2f);
        setColour(juce::DocumentWindow::textColourId, textColor);

        // Scrollbar colors - neutral cool grey-blue (more subdued than slider thumbs)
        auto scrollbarBackground = baseBackground.brighter(0.05f); // Slightly lighter than base
        auto scrollbarThumb = juce::Colour(0xff3a4555);            // Cool neutral grey-blue

        setColour(juce::ScrollBar::backgroundColourId, scrollbarBackground);
        setColour(juce::ScrollBar::thumbColourId, scrollbarThumb);
        setColour(juce::ScrollBar::trackColourId, scrollbarBackground);

        // Set this as the default LookAndFeel
        juce::LookAndFeel::setDefaultLookAndFeel(this);
    }

    // Override scrollbar width to make handles thicker
    int getDefaultScrollbarWidth() override
    {
        return 16; // Default is 12, so 16 is 33% thicker
    }

    void drawDocumentWindowTitleBar(
        juce::DocumentWindow& window,
        juce::Graphics& g,
        int w,
        int h,
        int titleSpaceX,
        int titleSpaceW,
        const juce::Image* /*icon*/,
        bool drawTitleTextOnLeft
    ) override
    {
        g.fillAll(myCustomColour);

        juce::String title = window.getName();
        g.setColour(findColour(juce::DocumentWindow::textColourId));
        g.setFont(juce::FontOptions(h * 0.65f, juce::Font::bold));

        if (drawTitleTextOnLeft)
            g.drawText(title, titleSpaceX + 4, 0, titleSpaceW - 8, h, juce::Justification::centredLeft, true);
        else
            g.drawText(title, 0, 0, w, h, juce::Justification::centred, true);
    }

    //==============================================================================
    class LookAndFeel_V4_DocumentWindowButton final : public juce::Button
    {
    public:
        LookAndFeel_V4_DocumentWindowButton(
            const juce::String& name,
            juce::Colour c,
            const juce::Path& normal,
            const juce::Path& toggled
        )
            : Button(name)
            , colour(c)
            , normalShape(normal)
            , toggledShape(toggled)
        {
        }

        void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
        {
            auto background = juce::Colours::grey;

            if (auto* rw = findParentComponentOfClass<juce::ResizableWindow>())
                if (auto lf = dynamic_cast<LookAndFeel_V4*>(&rw->getLookAndFeel()))
                    background = lf->findColour(juce::ResizableWindow::backgroundColourId); // == myCustomColour;

            g.fillAll(background);

            g.setColour((!isEnabled() || shouldDrawButtonAsDown) ? colour.withAlpha(0.6f) : colour);

            if (shouldDrawButtonAsHighlighted)
            {
                g.fillAll();
                g.setColour(background);
            }

            auto& p = getToggleState() ? toggledShape : normalShape;

            auto reducedRect = juce::Justification(juce::Justification::centred)
                                   .appliedToRectangle(juce::Rectangle<int>(getHeight(), getHeight()), getLocalBounds())
                                   .toFloat()
                                   .reduced((float)getHeight() * 0.3f);

            g.fillPath(p, p.getTransformToScaleToFit(reducedRect, true));
        }

    private:
        juce::Colour colour;
        juce::Path normalShape, toggledShape;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LookAndFeel_V4_DocumentWindowButton)
    };

    juce::Button* createDocumentWindowButton(int buttonType)
    {
        juce::Path shape;
        auto crossThickness = 0.15f;

        if (buttonType == juce::DocumentWindow::closeButton)
        {
            shape.addLineSegment({0.0f, 0.0f, 1.0f, 1.0f}, crossThickness);
            shape.addLineSegment({1.0f, 0.0f, 0.0f, 1.0f}, crossThickness);

            return new LookAndFeel_V4_DocumentWindowButton("close", juce::Colour(0xff9A131D), shape, shape);
        }

        if (buttonType == juce::DocumentWindow::minimiseButton)
        {
            shape.addLineSegment({0.0f, 0.5f, 1.0f, 0.5f}, crossThickness);

            return new LookAndFeel_V4_DocumentWindowButton("minimise", juce::Colour(0xffaa8811), shape, shape);
        }

        if (buttonType == juce::DocumentWindow::maximiseButton)
        {
            shape.addLineSegment({0.5f, 0.0f, 0.5f, 1.0f}, crossThickness);
            shape.addLineSegment({0.0f, 0.5f, 1.0f, 0.5f}, crossThickness);

            juce::Path fullscreenShape;
            fullscreenShape.startNewSubPath(45.0f, 100.0f);
            fullscreenShape.lineTo(0.0f, 100.0f);
            fullscreenShape.lineTo(0.0f, 0.0f);
            fullscreenShape.lineTo(100.0f, 0.0f);
            fullscreenShape.lineTo(100.0f, 45.0f);
            fullscreenShape.addRectangle(45.0f, 45.0f, 100.0f, 100.0f);
            juce::PathStrokeType(30.0f).createStrokedPath(fullscreenShape, fullscreenShape);

            return new LookAndFeel_V4_DocumentWindowButton(
                "maximise",
                juce::Colour(0xff0A830A),
                shape,
                fullscreenShape
            );
        }

        jassertfalse;
        return nullptr;
    }

    juce::Colour myCustomColour;
};

// Shared LookAndFeel wrapper for use with SharedResourcePointer
struct SharedJuceSonicLookAndFeel
{
    JuceSonicLookAndFeel lf;
};
