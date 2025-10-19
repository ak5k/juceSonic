#pragma once

#include "../jsfx/include/jsfx.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Windows.h (included by jsfx.h) defines Notification as a macro, which conflicts with JUCE
#ifdef Notification
#undef Notification
#endif

class JsfxHelper;

/**
 * JUCE component that renders JSFX @gfx section by reading the LICE framebuffer
 * directly from the JSFX instance. This avoids the need for platform-specific
 * window embedding.
 *
 * The component polls the LICE framebuffer at the display refresh rate and
 * forwards mouse/keyboard events to JSFX.
 */
class JsfxLiceComponent
    : public juce::Component
    , private juce::Timer
{
public:
    JsfxLiceComponent(SX_Instance* instance, JsfxHelper& helper);
    ~JsfxLiceComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Timer callback to poll LICE framebuffer
    void timerCallback() override;

    // Get recommended size from JSFX gfx_w/gfx_h
    juce::Rectangle<int> getRecommendedBounds();

private:
    // Helper methods for mouse event forwarding
    void updateMousePosition(const juce::MouseEvent& event);
    void updateMouseButtons(const juce::MouseEvent& event);

    // Helper method to trigger JSFX graphics initialization
    void triggerJsfxGraphicsInit();

    // Helper method to immediately execute @gfx code (for interactive updates)
    void triggerGfxExecution();

    SX_Instance* instance;
    JsfxHelper& helper;

    // For future use when we implement LICE rendering
    int lastFramebufferWidth = 0;
    int lastFramebufferHeight = 0;
    
    // Cached JUCE image to avoid allocating on every paint call
    juce::Image cachedLiceImage;
};
