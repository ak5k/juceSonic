#include "JsfxNativeWindow.h"

#include "build/_deps/jsfx-src/jsfx/sfxui.h"

#include <juce_gui_basics/juce_gui_basics.h>

#ifdef _WIN32
#include <windows.h>
#endif

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
#ifdef _WIN32
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
#endif
    }

    void destroyNative()
    {
#ifdef _WIN32
        if (nativeHwnd)
        {
            if (sxInstance)
                sx_deleteUI(sxInstance);
            DestroyWindow(nativeHwnd);
            nativeHwnd = nullptr;
        }
#endif
    }

    void resized() override
    {
#ifdef _WIN32
        if (nativeHwnd)
        {
            auto r = getLocalBounds();
            SetWindowPos(nativeHwnd, NULL, 0, 0, r.getWidth(), r.getHeight(), SWP_NOZORDER | SWP_NOACTIVATE);
        }
#endif
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);
    }

private:
    SX_Instance* sxInstance = nullptr;
#ifdef _WIN32
    HWND nativeHwnd = nullptr;
#endif
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
