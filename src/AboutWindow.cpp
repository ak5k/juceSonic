#include "AboutWindow.h"
#include "PluginConstants.h"
#include "BinaryData.h" // Include the generated binary data header

AboutWindow::AboutWindow()
    : DocumentWindow(
          "About " + juce::String(juce::CharPointer_UTF8(JucePlugin_Name)),
          juce::Colours::darkgrey, // Temporary color, will be updated after sharedLookAndFeel is initialized
          DocumentWindow::closeButton
      )
{
    // Now sharedLookAndFeel is initialized, set the proper LookAndFeel
    setLookAndFeel(&sharedLookAndFeel->lf);

    // Update background color from LookAndFeel
    setBackgroundColour(sharedLookAndFeel->lf.findColour(juce::ResizableWindow::backgroundColourId));

    setUsingNativeTitleBar(true);
    setContentOwned(new ContentComponent(), true);

    // Make window larger and resizable so long license lines can be viewed
    centreWithSize(800, 600);
    setResizable(true, true);
    setResizeLimits(600, 400, 1400, 1000);
    setVisible(true);
}

AboutWindow::~AboutWindow()
{
    setLookAndFeel(nullptr);
}

void AboutWindow::closeButtonPressed()
{
    delete this;
}

//==============================================================================
AboutWindow::ContentComponent::ContentComponent()
{
    // Title label
    addAndMakeVisible(titleLabel);
    titleLabel.setText(juce::CharPointer_UTF8(JucePlugin_Name), juce::dontSendNotification);
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);

    // Copyright label
    addAndMakeVisible(copyrightLabel);
    juce::String copyrightText =
        juce::String("Copyright (c) ") + juce::String(juce::CharPointer_UTF8(JucePlugin_Manufacturer));
    copyrightLabel.setText(copyrightText, juce::dontSendNotification);
    copyrightLabel.setJustificationType(juce::Justification::centred);

    // License text editor with word wrap
    addAndMakeVisible(licenseTextEditor);
    licenseTextEditor.setMultiLine(true, true); // multi-line with word wrap
    licenseTextEditor.setReadOnly(true);
    licenseTextEditor.setScrollbarsShown(true);
    licenseTextEditor.setCaretVisible(false);
    licenseTextEditor.setPopupMenuEnabled(true);
    licenseTextEditor.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));

    // Load licenses from binary data (UTF-8 encoded)
    juce::String licenseText(
        juce::CharPointer_UTF8(BinaryData::LicensesBinaryData_txt),
        BinaryData::LicensesBinaryData_txtSize
    );
    licenseTextEditor.setText(licenseText, false);

    // Close button
    addAndMakeVisible(closeButton);
    closeButton.setButtonText("Close");
    closeButton.onClick = [this]()
    {
        // Get the AboutWindow parent and trigger its close button
        if (auto* aboutWindow = findParentComponentOfClass<AboutWindow>())
            aboutWindow->closeButtonPressed();
    };

    setSize(800, 600);
}

void AboutWindow::ContentComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void AboutWindow::ContentComponent::resized()
{
    auto area = getLocalBounds().reduced(20);

    titleLabel.setBounds(area.removeFromTop(40));
    copyrightLabel.setBounds(area.removeFromTop(30));

    area.removeFromTop(10); // Spacing

    auto buttonArea = area.removeFromBottom(40);
    closeButton.setBounds(buttonArea.withSizeKeepingCentre(100, 30));

    area.removeFromBottom(10); // Spacing

    licenseTextEditor.setBounds(area);
}
