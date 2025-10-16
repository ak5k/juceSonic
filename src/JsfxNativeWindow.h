#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct SX_Instance;
class JsfxHelper;

#ifdef _WIN32
#include <windows.h>
#endif

// Forward declare HWND for cross-platform compatibility
#ifndef _WIN32
struct HWND__;
typedef HWND__* HWND;
#endif

/**
 * Cross-platform native JSFX UI window wrapper
 *
 * Creates a resizable native window that hosts the JSFX UI dialog:
 * - Windows: Standalone Win32 window with WS_OVERLAPPEDWINDOW style
 * - Linux/macOS: SWELL window (Win32 API emulation)
 *
 * Architecture:
 * - Parent window: Resizable container with title bar and borders
 * - Child dialog: JSFX UI created by sx_createUI()
 * - WM_SIZE handler: Automatically resizes child to match parent
 * - I/O button: Handled via WM_USER+1030 message
 */
class JsfxNativeWindow
{
public:
    JsfxNativeWindow(SX_Instance* instance, const juce::String& title, JsfxHelper& helper);
    ~JsfxNativeWindow();

    void setVisible(bool shouldBeVisible);
    void setAlwaysOnTop(bool shouldBeOnTop);

    // Callback for I/O button click (optional - provides custom I/O matrix UI)
    std::function<void()> onIOButtonClicked;

    // Public access for message handling
    JsfxHelper& jsfxHelper;
    void* nativeUIHandle = nullptr;     // HWND of JSFX child dialog
    void* parentWindowHandle = nullptr; // HWND of parent window

    // I/O button handler (called from window/dialog proc)
    void handleJsfxIORequest(SX_Instance* jsfxInstance, HWND jsfxDialog);

private:
    SX_Instance* sxInstance = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxNativeWindow)
};
