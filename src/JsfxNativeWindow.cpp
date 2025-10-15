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
    HostComponent(SX_Instance* inst, JsfxHelper& helper)
        : sxInstance(inst)
        , jsfxHelper(helper)
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
        nativeUIHandle = jsfxHelper.createJsfxUI(sxInstance, parent);

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
            // Use the helper reference directly
            jsfxHelper.destroyJsfxUI(sxInstance, nativeUIHandle);
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
    JsfxHelper& jsfxHelper;
    void* nativeUIHandle = nullptr;
};
#endif

#ifdef _WIN32
JsfxNativeWindow::JsfxNativeWindow(SX_Instance* instance, const juce::String& title, JsfxHelper& helper)
    : DocumentWindow(title, juce::Colours::darkgrey, allButtons)
    , sxInstance(instance)
    , jsfxHelper(helper)
{
    // On Windows, use HostComponent to embed the JSFX UI
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    host = std::make_unique<HostComponent>(sxInstance, jsfxHelper);
    setContentNonOwned(host.get(), true);

    // Start with a small default size - will be resized based on actual dialog size
    centreWithSize(100, 100);
    setVisible(true);

    // Create the native UI - this will trigger resizeForDialog callback
    host->createNative();

    // Setup Windows message handling for JSFX notifications (like I/O button)
    setupWindowMessageHandler();
}
#else
JsfxNativeWindow::JsfxNativeWindow(SX_Instance* instance, const juce::String& title, JsfxHelper& helper)
    : sxInstance(instance)
    , jsfxHelper(helper)
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

    // Static dialog proc for parent window with I/O button support
    static DLGPROC parentDialogProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> INT_PTR
    {
        // Handle I/O button message (WM_USER+1030)
        if (msg == WM_USER + 1030)
        {
            // Get JsfxNativeWindow instance from parent window user data
            JsfxNativeWindow* self = (JsfxNativeWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            if (self)
            {
                SX_Instance* jsfxInstance = (SX_Instance*)wParam;
                HWND jsfxDialog = (HWND)lParam;
                self->handleJsfxIORequest(jsfxInstance, jsfxDialog);
                return 1;
            }
        }

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

    // Store 'this' pointer in parent window user data for I/O button handling
    SetWindowLongPtr(parentWindow, GWLP_USERDATA, (LONG_PTR)this);

    // Now create JSFX UI as child of parent window
    nativeUIHandle = jsfxHelper.createJsfxUI(sxInstance, parentWindow);

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

        // Center the parent window on screen
        // Get screen dimensions
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenWidth - childWidth) / 2;
        int y = (screenHeight - childHeight) / 2;

        // Ensure window is not off-screen
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;

        // Since SWELL doesn't seem to add window decorations that we can measure,
        // just make the parent window client area match the child size
        // The client area IS the full window with SWELL (no decorations offset)
        SetWindowPos(parentWindow, HWND_TOP, x, y, childWidth, childHeight, SWP_SHOWWINDOW);

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
    // Remove message handler before destroying
    removeWindowMessageHandler();

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
        jsfxHelper.destroyJsfxUI(sxInstance, childToDestroy);
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

void JsfxNativeWindow::setupWindowMessageHandler()
{
    // Get the native Windows handle from JUCE
    HWND hwnd = (HWND)getWindowHandle();
    if (!hwnd)
        return;

    // Store 'this' pointer in window user data so windowProc can access it
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);

    // Subclass the window to intercept messages
    originalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)windowProc);
}

void JsfxNativeWindow::removeWindowMessageHandler()
{
    HWND hwnd = (HWND)getWindowHandle();
    if (!hwnd || !originalWndProc)
        return;

    // Restore original window procedure
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)originalWndProc);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    originalWndProc = nullptr;
}

LRESULT CALLBACK JsfxNativeWindow::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Get the JsfxNativeWindow instance from user data
    JsfxNativeWindow* self = (JsfxNativeWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (self && msg == WM_USER + 1030)
    {
        // JSFX I/O button was clicked
        // wParam = SX_Instance*, lParam = HWND of JSFX dialog
        SX_Instance* jsfxInstance = (SX_Instance*)wParam;
        HWND jsfxDialog = (HWND)lParam;

        self->handleJsfxIORequest(jsfxInstance, jsfxDialog);
        return 0;
    }

    // Call original window procedure for all other messages
    if (self && self->originalWndProc)
        return CallWindowProc(self->originalWndProc, hwnd, msg, wParam, lParam);

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
#else
void JsfxNativeWindow::setVisible(bool shouldBeVisible)
{
    // On Linux/macOS, control visibility of the parent window (child follows parent)
    if (parentWindowHandle)
    {
        HWND parentHwnd = static_cast<HWND>(parentWindowHandle);
        ShowWindow(parentHwnd, shouldBeVisible ? SW_SHOW : SW_HIDE);
    }
}

void JsfxNativeWindow::setAlwaysOnTop(bool shouldBeOnTop)
{
    // On Linux/macOS, this is a no-op for now
    // Could potentially use SWELL window management functions if needed
    (void)shouldBeOnTop;
}

#endif

// Cross-platform I/O button handler (uses SWELL on Linux/macOS, native Win32 on Windows)
void JsfxNativeWindow::handleJsfxIORequest(SX_Instance* jsfxInstance, HWND jsfxDialog)
{
    // If a callback is set, use it to show the I/O Matrix window
    if (onIOButtonClicked)
    {
        onIOButtonClicked();
        return;
    }

    // Fallback: Show simple info dialog if no callback is set
    // Get pin information from JSFX (same sx_getPinInfo works on all platforms via SWELL)
    extern const char** sx_getPinInfo(SX_Instance * sx, int isOutput, int* numPins);

    int numInputs = 0, numOutputs = 0;
    const char** inputPins = sx_getPinInfo(jsfxInstance, 0, &numInputs);   // 0 = inputs
    const char** outputPins = sx_getPinInfo(jsfxInstance, 1, &numOutputs); // 1 = outputs

    // Create a message showing I/O info with pin names
    juce::String message;
    message << "JSFX I/O Configuration\n\n";

    message << "Input Pins: " << numInputs << "\n";
    if (inputPins && numInputs > 0)
    {
        for (int i = 0; i < numInputs && i < 8; ++i) // Show first 8
            if (inputPins[i])
                message << "  " << (i + 1) << ". " << juce::String(juce::CharPointer_UTF8(inputPins[i])) << "\n";
        if (numInputs > 8)
            message << "  ... and " << (numInputs - 8) << " more\n";
    }

    message << "\nOutput Pins: " << numOutputs << "\n";
    if (outputPins && numOutputs > 0)
    {
        for (int i = 0; i < numOutputs && i < 8; ++i) // Show first 8
            if (outputPins[i])
                message << "  " << (i + 1) << ". " << juce::String(juce::CharPointer_UTF8(outputPins[i])) << "\n";
        if (numOutputs > 8)
            message << "  ... and " << (numOutputs - 8) << " more\n";
    }

    message << "\nNote: Custom I/O Matrix can be configured via callback";

    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "I/O Configuration", message, "OK");
}
