#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct SX_Instance;

class JsfxNativeWindow : public juce::DocumentWindow
{
public:
    JsfxNativeWindow(SX_Instance* instance, const juce::String& title);
    ~JsfxNativeWindow() override;

    void closeButtonPressed() override;

    // Called by HostComponent when JSFX dialog size is determined
    void resizeForDialog(int width, int height);

private:
    class HostComponent;
    std::unique_ptr<HostComponent> host;
    SX_Instance* sxInstance = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxNativeWindow)
};
