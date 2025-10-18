#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "JuceSonicLookAndFeel.h"

/**
 * @brief About window displaying plugin information and licenses
 */
class AboutWindow : public juce::DocumentWindow
{
public:
    AboutWindow();
    ~AboutWindow() override;

    void closeButtonPressed() override;

private:
    class ContentComponent;
    juce::SharedResourcePointer<SharedJuceSonicLookAndFeel> sharedLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutWindow)
};

/**
 * @brief Content component for the About window
 */
class AboutWindow::ContentComponent : public juce::Component
{
public:
    ContentComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::Label copyrightLabel;
    juce::TextEditor licenseTextEditor;
    juce::TextButton closeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentComponent)
};
