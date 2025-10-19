#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class JsfxLiceComponent;

/**
 * Fullscreen window for JSFX LICE renderer.
 * Borrows the component from PluginEditor, doesn't own it.
 */
class JsfxLiceFullscreenWindow : public juce::ResizableWindow
{
public:
    JsfxLiceFullscreenWindow();
    ~JsfxLiceFullscreenWindow() override = default;

    bool keyPressed(const juce::KeyPress& key) override;
    void closeButtonPressed();

    void showWithComponent(JsfxLiceComponent* component);

    std::function<void()> onWindowClosed;
    std::function<void()> onToggleButtonBar;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxLiceFullscreenWindow)
};
