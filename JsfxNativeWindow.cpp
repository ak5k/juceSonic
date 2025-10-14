#include "JsfxNativeWindow.h"

#include "build/_deps/jsfx-src/jsfx/sfxui.h"

#include <juce_gui_basics/juce_gui_basics.h>

// SWELL provides Win32 API compatibility on all platforms
#include "build/_deps/jsfx-src/WDL/swell/swell.h"

class JsfxNativeWindow::HostComponent : public juce::Component
{
public:
    HostComponent(SX_Instance* inst)
        : sxInstance(inst)
    {
        setOpaque(true);
    }

    ~HostComponent() override
    {
        destroyNative();
    }

    void createNative()
    {
        if (nativeHwnd || !sxInstance)
            return;
        HWND parent = (HWND)getWindowHandle();
        if (!parent)
            return;
        // Pass hostpostparam matching sx->m_hostctx (set in loadJSFX via sx_set_host_ctx)
        nativeHwnd = sx_createUI(
            sxInstance,
            (HINSTANCE)juce::Process::getCurrentModuleInstanceHandle(),
            parent,
            sxInstance->m_hostctx
        );
        if (nativeHwnd)
        {
            auto r = getLocalBounds();
            SetWindowPos(
                nativeHwnd,
                NULL,
                0,
                0,
                r.getWidth(),
                r.getHeight(),
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW
            );
            ShowWindow(nativeHwnd, SW_SHOWNA);
        }
    }

    void destroyNative()
    {
        if (nativeHwnd)
        {
            if (sxInstance)
                sx_deleteUI(sxInstance);
            DestroyWindow(nativeHwnd);
            nativeHwnd = nullptr;
        }
    }

    void resized() override
    {
        if (nativeHwnd)
        {
            auto r = getLocalBounds();
            SetWindowPos(nativeHwnd, NULL, 0, 0, r.getWidth(), r.getHeight(), SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);
    }

private:
    SX_Instance* sxInstance = nullptr;
    HWND nativeHwnd = nullptr;
};

JsfxNativeWindow::JsfxNativeWindow(SX_Instance* instance, const juce::String& title)
    : DocumentWindow(title, juce::Colours::darkgrey, allButtons)
    , sxInstance(instance)
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    host = std::make_unique<HostComponent>(sxInstance);
    setContentNonOwned(host.get(), true);
    centreWithSize(500, 400);
    setVisible(true);
    host->createNative();
}

JsfxNativeWindow::~JsfxNativeWindow()
{
    if (host)
        host->destroyNative();
}

void JsfxNativeWindow::closeButtonPressed()
{
    if (host)
        host->destroyNative();
    setVisible(false);
}
