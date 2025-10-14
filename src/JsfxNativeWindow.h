#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct SX_Instance;

#ifdef _WIN32
// On Windows, use a DocumentWindow to host the embedded JSFX UI
class JsfxNativeWindow : public juce::DocumentWindow
{
public:
    JsfxNativeWindow(SX_Instance* instance, const juce::String& title);
    ~JsfxNativeWindow() override;

    void closeButtonPressed() override;
    void setVisible(bool shouldBeVisible) override;

    // Called by HostComponent when JSFX dialog size is determined
    void resizeForDialog(int width, int height);

private:
    class HostComponent;
    std::unique_ptr<HostComponent> host;
    SX_Instance* sxInstance = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxNativeWindow)
};
#else
// On Linux, just a simple wrapper that manages the SWELL HWND
// No JUCE window is created - SWELL creates its own top-level window
class JsfxNativeWindow
{
public:
    JsfxNativeWindow(SX_Instance* instance, const juce::String& title);
    ~JsfxNativeWindow();

    void setVisible(bool shouldBeVisible);
    void setAlwaysOnTop(bool shouldBeOnTop);

private:
    SX_Instance* sxInstance = nullptr;
    void* nativeUIHandle = nullptr;     // HWND of JSFX child dialog
    void* parentWindowHandle = nullptr; // HWND of parent window

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxNativeWindow)
};
#endif
