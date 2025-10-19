#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class JsfxLiceComponent;

/**
 * Fullscreen window for JSFX LICE renderer.
 */
class JsfxLiceFullscreenWindow : public juce::ResizableWindow
{
public:
    JsfxLiceFullscreenWindow();
    ~JsfxLiceFullscreenWindow() override = default;

    bool keyPressed(const juce::KeyPress& key) override;
    void closeButtonPressed();

    void showWithComponent(JsfxLiceComponent* liceComponent);

    std::function<void()> onWindowClosed;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxLiceFullscreenWindow)
};
