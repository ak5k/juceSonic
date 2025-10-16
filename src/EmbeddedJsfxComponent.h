#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

struct SX_Instance;
class JsfxHelper;

/**
 * Cross-platform JSFX UI host component.
 * 
 * Platform-specific behavior:
 * - Windows/Mac: Embeds JSFX window using JUCE's native window handle
 * - Linux: Creates independent floating window (SWELL/GTK limitation)
 */
class EmbeddedJsfxComponent
    : public juce::Component
    , private juce::Timer
{
public:
    EmbeddedJsfxComponent(SX_Instance* instance, JsfxHelper& helper);
    ~EmbeddedJsfxComponent() override;

    void createNative();
    void destroyNative();

    void resized() override;
    void paint(juce::Graphics& g) override;
    void visibilityChanged() override;
    void timerCallback() override;

    bool isNativeCreated() const
    {
        return nativeCreated;
    }

    void* getNativeHandle() const
    {
        return nativeUIHandle;
    }

    int getJsfxWindowWidth() const
    {
        return jsfxWindowWidth;
    }

    int getJsfxWindowHeight() const
    {
        return jsfxWindowHeight;
    }

    // Callback when native UI is created with its initial size
    std::function<void(int width, int height)> onNativeCreated;

private:
    SX_Instance* sxInstance = nullptr;
    JsfxHelper& jsfxHelper;

    void* nativeUIHandle = nullptr;
    bool nativeCreated = false;
    int createRetryCount = 0;
    int jsfxWindowWidth = 0;
    int jsfxWindowHeight = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EmbeddedJsfxComponent)
};