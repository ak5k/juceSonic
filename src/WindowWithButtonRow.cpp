#include "WindowWithButtonRow.h"

WindowWithButtonRow::WindowWithButtonRow()
{
    setLookAndFeel(&sharedLookAndFeel->lf);

    addAndMakeVisible(buttonRow);
    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);
}

WindowWithButtonRow::~WindowWithButtonRow()
{
    setLookAndFeel(nullptr);
}

void WindowWithButtonRow::resized()
{
    auto bounds = getLocalBounds().reduced(4);

    // Top button row (if visible)
    if (buttonRow.isVisible())
    {
        const int buttonHeight = 30; // Fixed height - don't make proportional to avoid resizing when content changes
        auto topButtons = bounds.removeFromTop(buttonHeight);
        buttonRow.setBounds(topButtons);
        bounds.removeFromTop(4);
    }

    // Status label at bottom (if visible)
    if (statusLabel.isVisible())
    {
        statusLabel.setBounds(bounds.removeFromBottom(20));
        bounds.removeFromBottom(4);
    }

    // Main content fills remaining space
    if (auto* mainComp = getMainComponent())
        mainComp->setBounds(bounds);
}

void WindowWithButtonRow::setControlsVisible(bool visible)
{
    buttonRow.setVisible(visible);
    statusLabel.setVisible(visible);
    resized();
}
