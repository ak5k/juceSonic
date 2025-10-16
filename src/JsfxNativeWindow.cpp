/**
 * JsfxNativeWindow.cpp
 *
 * Cross-platform implementation of native JSFX UI window.
 * Creates a resizable parent window that hosts the JSFX dialog as a child.
 *
 * Architecture:
 * ┌─────────────────────────────────────┐
 * │  Parent Window (Resizable)          │  ← User resizes this
 * │  ┌───────────────────────────────┐  │
 * │  │ JSFX Dialog (Child)           │  │  ← Auto-resized via WM_SIZE
 * │  │ - Sliders                     │  │
 * │  │ - Buttons                     │  │
 * │  │ - GFX Window                  │  │
 * │  └───────────────────────────────┘  │
 * └─────────────────────────────────────┘
 *
 * Implementation:
 * - Windows: Win32 window with WS_OVERLAPPEDWINDOW style
 * - Linux/macOS: SWELL window (Win32 API emulation)
 * - Common message handler: HandleParentWindowMessage()
 * - Common setup: SetupJsfxChildDialog()
 *
 * Key features:
 * ✓ User-resizable with drag handles
 * ✓ WM_SIZE automatically resizes JSFX child
 * ✓ I/O button support (WM_USER+1030)
 * ✓ 95% unified codebase between platforms
 */ \
#include "JsfxNativeWindow.h"

#include "JsfxHelper.h"

// Include sfxui.h BEFORE JUCE to get SWELL/Windows definitions
// sfxui.h includes <windows.h> on Windows or "../WDL/swell/swell.h" on Linux
#include <jsfx/sfxui.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Common message handler for parent window (works on both Win32 and SWELL)
static INT_PTR HandleParentWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, JsfxNativeWindow* self)
{
    switch (msg)
    {
    case WM_USER + 1030: // I/O button message (cross-platform)
        if (self)
        {
            SX_Instance* jsfxInstance = (SX_Instance*)wParam;
            HWND jsfxDialog = (HWND)lParam;
            self->handleJsfxIORequest(jsfxInstance, jsfxDialog);
            return 1;
        }
        break;

    case WM_SIZE:
        if (self && self->nativeUIHandle && wParam != SIZE_MINIMIZED)
        {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int width = clientRect.right - clientRect.left;
            int height = clientRect.bottom - clientRect.top;

            HWND childHwnd = static_cast<HWND>(self->nativeUIHandle);

            // Resize child JSFX dialog to match parent client area
            SetWindowPos(childHwnd, HWND_TOP, 0, 0, width, height, SWP_NOZORDER | SWP_NOCOPYBITS);
        }
        return 0;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 1; // Handled

    case WM_DESTROY:
        return 0;
    }
    return -1; // Not handled
}

// Common setup for JSFX child dialog after parent window is created
static void SetupJsfxChildDialog(HWND parentWindow, JsfxNativeWindow* self, JsfxHelper& helper, SX_Instance* sxInstance)
{
    // Create JSFX UI as child of parent window
    self->nativeUIHandle = helper.createJsfxUI(sxInstance, parentWindow);

    if (!self->nativeUIHandle)
    {
        DBG("Failed to create JSFX UI child dialog");
        DestroyWindow(parentWindow);
        return;
    }

    HWND childHwnd = static_cast<HWND>(self->nativeUIHandle);

    // Get child dialog dimensions
    RECT childRect;
    GetClientRect(childHwnd, &childRect);
    int childWidth = childRect.right - childRect.left;
    int childHeight = childRect.bottom - childRect.top;

    // Position child at origin of parent's client area
    SetWindowPos(childHwnd, HWND_TOP, 0, 0, childWidth, childHeight, SWP_NOZORDER);

#ifdef _WIN32
    // Windows: Adjust parent window size to include decorations
    RECT windowRect = {0, 0, childWidth, childHeight};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    SetWindowPos(parentWindow, NULL, 0, 0, windowWidth, windowHeight, SWP_NOMOVE | SWP_NOZORDER);
#else
    // SWELL: No window decorations, parent client area = window size
    // Center parent window on screen
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = juce::jmax(0, (screenWidth - childWidth) / 2);
    int y = juce::jmax(0, (screenHeight - childHeight) / 2);

    SetWindowPos(parentWindow, HWND_TOP, x, y, childWidth, childHeight, SWP_SHOWWINDOW);
    SetForegroundWindow(parentWindow);
#endif

    // Show child window
    ShowWindow(childHwnd, SW_SHOW);
    ShowWindow(parentWindow, SW_SHOW);
    UpdateWindow(parentWindow);
}

#ifdef _WIN32
// Windows-specific window procedure
static LRESULT CALLBACK ParentWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    JsfxNativeWindow* self = (JsfxNativeWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    INT_PTR result = HandleParentWindowMessage(hwnd, msg, wParam, lParam, self);
    if (result != -1)
        return result;

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

JsfxNativeWindow::JsfxNativeWindow(SX_Instance* instance, const juce::String& title, JsfxHelper& helper)
    : sxInstance(instance)
    , jsfxHelper(helper)
{
    // Register window class (once)
    static bool classRegistered = false;
    const wchar_t* className = L"JsfxNativeWindowClass";

    if (!classRegistered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ParentWindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        classRegistered = true;
    }

    // Create resizable parent window
    HWND parentWindow = CreateWindowExW(
        0,
        className,
        title.toWideCharPointer(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        400,
        300, // Initial size (will be adjusted)
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (!parentWindow)
    {
        DBG("Failed to create parent window for JSFX UI");
        return;
    }

    parentWindowHandle = parentWindow;
    SetWindowLongPtr(parentWindow, GWLP_USERDATA, (LONG_PTR)this);

    // Common setup for JSFX child dialog
    SetupJsfxChildDialog(parentWindow, this, jsfxHelper, sxInstance);
}
#else
JsfxNativeWindow::JsfxNativeWindow(SX_Instance* instance, const juce::String& title, JsfxHelper& helper)
    : sxInstance(instance)
    , jsfxHelper(helper)
{
    // SWELL (Linux/macOS): Create parent window using SWELL_CreateDialog
    // Special resid 0x400001 creates a resizable top-level window
    extern SWELL_DialogResourceIndex* SWELL_curmodule_dialogresource_head;
    extern HWND SWELL_CreateDialog(SWELL_DialogResourceIndex*, const char*, HWND, DLGPROC, LPARAM);

    // SWELL dialog proc - uses common message handler
    static DLGPROC parentDialogProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> INT_PTR
    {
        JsfxNativeWindow* self = (JsfxNativeWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        INT_PTR result = HandleParentWindowMessage(hwnd, msg, wParam, lParam, self);
        if (result != -1)
            return result;

        return 0; // SWELL dialog proc returns 0 for unhandled
    };

    HWND parentWindow = SWELL_CreateDialog(
        SWELL_curmodule_dialogresource_head,
        (const char*)(INT_PTR)0x400001, // Resizable window
        nullptr,                        // No parent (top-level)
        parentDialogProc,
        0
    );

    if (!parentWindow)
    {
        DBG("Failed to create parent window for JSFX UI");
        return;
    }

    if (title.isNotEmpty())
        SetWindowText(parentWindow, title.toRawUTF8());

    parentWindowHandle = parentWindow;
    SetWindowLongPtr(parentWindow, GWLP_USERDATA, (LONG_PTR)this);

    // Common setup for JSFX child dialog
    SetupJsfxChildDialog(parentWindow, this, jsfxHelper, sxInstance);
}
#endif

JsfxNativeWindow::~JsfxNativeWindow()
{
    // Destroy the JSFX UI windows
    // Store handles locally and clear members first to prevent
    // any callbacks during destruction from trying to use them
    void* childToDestroy = nativeUIHandle;
    void* parentToDestroy = parentWindowHandle;
    nativeUIHandle = nullptr;
    parentWindowHandle = nullptr;

    // First destroy the child JSFX dialog
    if (childToDestroy)
        jsfxHelper.destroyJsfxUI(sxInstance, childToDestroy);

    // Then destroy the parent window
    if (parentToDestroy)
        DestroyWindow(static_cast<HWND>(parentToDestroy));
}

void JsfxNativeWindow::setVisible(bool shouldBeVisible)
{
    // Control visibility of the parent window (child follows parent on all platforms)
    if (parentWindowHandle)
    {
        HWND parentHwnd = static_cast<HWND>(parentWindowHandle);
        ShowWindow(parentHwnd, shouldBeVisible ? SW_SHOW : SW_HIDE);
    }
}

void JsfxNativeWindow::setAlwaysOnTop(bool shouldBeOnTop)
{
#ifdef _WIN32
    if (parentWindowHandle)
    {
        HWND parentHwnd = static_cast<HWND>(parentWindowHandle);
        SetWindowPos(parentHwnd, shouldBeOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
#else
    // On Linux/macOS with SWELL, this is a no-op for now
    (void)shouldBeOnTop;
#endif
}

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
