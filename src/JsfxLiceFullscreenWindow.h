#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class JsfxLiceComponent;

/**
 * Resizable window that can display JSFX LICE renderer in a separate window
 * and toggle fullscreen mode
 */
class JsfxLiceFullscreenWindow : public juce::ResizableWindow
{
public:
    JsfxLiceFullscreenWindow();
    ~JsfxLiceFullscreenWindow() override;

    bool keyPressed(const juce::KeyPress& key) override;
    void closeButtonPressed();
    void resized() override;

    // Show the window with the LICE component
    void showWithComponent(JsfxLiceComponent* liceComponent);

    // Callback when user wants to close the window
    std::function<void()> onWindowClosed;

private:
    JsfxLiceComponent* liceComponent = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxLiceFullscreenWindow)
};
