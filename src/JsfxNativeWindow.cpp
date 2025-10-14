#include "JsfxNativeWindow.h"

#include "JsfxHelper.h"

// Include sfxui.h BEFORE JUCE to get SWELL/Windows definitions
// sfxui.h includes <windows.h> on Windows or "../WDL/swell/swell.h" on Linux
#include <jsfx/sfxui.h>
#include <juce_gui_basics/juce_gui_basics.h>

#ifdef _WIN32
// HostComponent is only needed on Windows for embedding
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
        if (nativeUIHandle || !sxInstance)
            return;

        // On Windows, we can use the JUCE window as parent for embedding
        void* parent = getWindowHandle();
        if (!parent)
            return;

        // Create JSFX UI using helper (isolates Win32/SWELL from JUCE)
        nativeUIHandle = JsfxHelper::createJsfxUI(sxInstance, parent);

        if (nativeUIHandle)
        {
            // Get the actual size that the JSFX UI dialog wants to be
            auto uiSize = JsfxHelper::getJsfxUISize(nativeUIHandle);

            // Notify parent that we want to resize to accommodate the dialog
            if (auto* parentWindow = findParentComponentOfClass<JsfxNativeWindow>())
                parentWindow->resizeForDialog(uiSize.width, uiSize.height);

            // Position and show the dialog
            JsfxHelper::positionJsfxUI(nativeUIHandle, 0, 0, uiSize.width, uiSize.height);
            JsfxHelper::showJsfxUI(nativeUIHandle, true);
        }
    }

    void destroyNative()
    {
        if (nativeUIHandle)
        {
            JsfxHelper::destroyJsfxUI(sxInstance, nativeUIHandle);
            nativeUIHandle = nullptr;
        }
    }

    void resized() override
    {
        if (nativeUIHandle)
        {
            auto r = getLocalBounds();
            JsfxHelper::positionJsfxUI(nativeUIHandle, 0, 0, r.getWidth(), r.getHeight());
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);
    }

private:
    SX_Instance* sxInstance = nullptr;
    void* nativeUIHandle = nullptr;
};
#endif

#ifdef _WIN32
JsfxNativeWindow::JsfxNativeWindow(SX_Instance* instance, const juce::String& title)
    : DocumentWindow(title, juce::Colours::darkgrey, allButtons)
    , sxInstance(instance)
{
    // On Windows, use HostComponent to embed the JSFX UI
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    host = std::make_unique<HostComponent>(sxInstance);
    setContentNonOwned(host.get(), true);

    // Start with a small default size - will be resized based on actual dialog size
    centreWithSize(100, 100);
    setVisible(true);

    // Create the native UI - this will trigger resizeForDialog callback
    host->createNative();
}
#else
JsfxNativeWindow::JsfxNativeWindow(SX_Instance* instance, const juce::String& title)
    : sxInstance(instance)
{
    // On Linux: JSFX dialog (IDD_SXUI) is defined with SWELL_DLG_WS_CHILD style.
    // Solution: Create a SWELL parent window first, then create the JSFX dialog as a child.
    // This avoids the need to patch the dialog resource.
    //
    // SWELL message loop is pumped by the PluginEditor's timer (see PluginEditor::timerCallback).

    // Create a simple parent window using SWELL_CreateDialog
    // Special resid 0x400001: creates a resizable top-level window (see swell-dlg-generic.cpp)
    extern SWELL_DialogResourceIndex* SWELL_curmodule_dialogresource_head;
    extern HWND SWELL_CreateDialog(SWELL_DialogResourceIndex*, const char*, HWND, DLGPROC, LPARAM);

    // Static dialog proc for parent window
    static DLGPROC parentDialogProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> INT_PTR
    {
        switch (msg)
        {
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 1;
        case WM_DESTROY:
            return 0;
        }
        return 0;
    };

    HWND parentWindow = SWELL_CreateDialog(
        SWELL_curmodule_dialogresource_head,
        (const char*)(INT_PTR)0x400001, // Force resizable non-child window
        nullptr,                        // No parent - this is top level
        parentDialogProc,
        0
    );

    if (!parentWindow)
    {
        DBG("Failed to create parent window for JSFX UI");
        return;
    }

    // Set parent window title
    if (title.isNotEmpty())
        SetWindowText(parentWindow, title.toRawUTF8());

    // Store parent window handle
    parentWindowHandle = parentWindow;

    // Now create JSFX UI as child of parent window
    nativeUIHandle = JsfxHelper::createJsfxUI(sxInstance, parentWindow);

    if (nativeUIHandle)
    {
        HWND hwnd = static_cast<HWND>(nativeUIHandle);

        // Get the child dialog dimensions
        RECT childRect;
        GetClientRect(hwnd, &childRect);
        int childWidth = childRect.right - childRect.left;
        int childHeight = childRect.bottom - childRect.top;

        DBG("JsfxNativeWindow: JSFX child dialog dimensions: " << childWidth << "x" << childHeight);

        // Position child at origin of parent's client area (0,0)
        SetWindowPos(hwnd, HWND_TOP, 0, 0, childWidth, childHeight, SWP_NOZORDER);

        // Since SWELL doesn't seem to add window decorations that we can measure,
        // just make the parent window client area match the child size
        // The client area IS the full window with SWELL (no decorations offset)
        SetWindowPos(parentWindow, HWND_TOP, 100, 100, childWidth, childHeight, SWP_SHOWWINDOW);

        // Child window is shown as part of parent
        ShowWindow(hwnd, SW_SHOW);

        // Bring parent window to front
        SetForegroundWindow(parentWindow);

        DBG("JsfxNativeWindow: JSFX UI window created as child of parent window");
    }
    else
    {
        DBG("Failed to create JSFX UI child dialog");
        // Clean up parent window if child creation failed
        DestroyWindow(parentWindow);
    }
}
#endif

JsfxNativeWindow::~JsfxNativeWindow()
{
#ifdef _WIN32
    if (host)
        host->destroyNative();
#else
    // On Linux, destroy the SWELL UI windows
    // Store handles locally and clear members first to prevent
    // any callbacks during destruction from trying to use them
    void* childToDestroy = nativeUIHandle;
    void* parentToDestroy = parentWindowHandle;
    nativeUIHandle = nullptr;
    parentWindowHandle = nullptr;

    // First destroy the child JSFX dialog
    if (childToDestroy)
    {
        // Now that we have a proper parent, this won't crash
        // because GetParent() will return valid parentWindowHandle
        JsfxHelper::destroyJsfxUI(sxInstance, childToDestroy);
    }

    // Then destroy the parent window
    if (parentToDestroy)
        DestroyWindow(static_cast<HWND>(parentToDestroy));
#endif
}

#ifdef _WIN32
void JsfxNativeWindow::closeButtonPressed()
{
    if (host)
        host->destroyNative();
    DocumentWindow::setVisible(false);
}

void JsfxNativeWindow::setVisible(bool shouldBeVisible)
{
    DocumentWindow::setVisible(shouldBeVisible);
}

void JsfxNativeWindow::resizeForDialog(int width, int height)
{
    // Resize the content component to match the dialog size
    if (host)
        host->setSize(width, height);

    // Resize the window to accommodate the content plus title bar
    // The DocumentWindow will automatically add space for the title bar
    setSize(width, height + getTitleBarHeight());

    // Re-center the window with the new size
    centreWithSize(width, height + getTitleBarHeight());
}
#else
void JsfxNativeWindow::setVisible(bool shouldBeVisible)
{
    // On Linux, control visibility of the parent window (child follows parent)
    if (parentWindowHandle)
    {
        HWND parentHwnd = static_cast<HWND>(parentWindowHandle);
        ShowWindow(parentHwnd, shouldBeVisible ? SW_SHOW : SW_HIDE);
    }
}

void JsfxNativeWindow::setAlwaysOnTop(bool shouldBeOnTop)
{
    // On Linux, this is a no-op for now
    // Could potentially use SWELL window management functions if needed
    (void)shouldBeOnTop;
}
#endif
