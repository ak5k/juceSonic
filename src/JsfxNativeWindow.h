#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct SX_Instance;
class JsfxHelper;

#ifdef _WIN32
#include <windows.h>

// On Windows, use a DocumentWindow to host the embedded JSFX UI
class JsfxNativeWindow : public juce::DocumentWindow
{
public:
    JsfxNativeWindow(SX_Instance* instance, const juce::String& title, JsfxHelper& helper);
    ~JsfxNativeWindow() override;

    void closeButtonPressed() override;
    void setVisible(bool shouldBeVisible) override;

    // Called by HostComponent when JSFX dialog size is determined
    void resizeForDialog(int width, int height);

    // Allow HostComponent access to the helper
    JsfxHelper& jsfxHelper;

    // Message handler for JSFX messages (WM_USER+1030 for I/O button)
    // Works on all platforms via SWELL Win32 API emulation
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Callback for when I/O button is clicked
    std::function<void()> onIOButtonClicked;

    // I/O button handler (called from static windowProc)
    void handleJsfxIORequest(SX_Instance* jsfxInstance, HWND jsfxDialog);

private:
    class HostComponent;
    std::unique_ptr<HostComponent> host;
    SX_Instance* sxInstance = nullptr;

    // Store original window procedure for subclassing
    WNDPROC originalWndProc = nullptr;

    void setupWindowMessageHandler();
    void removeWindowMessageHandler();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxNativeWindow)
};
#else
// On Linux/macOS, use SWELL Win32 API emulation
// Forward declare HWND - actual definition comes from SWELL headers
struct HWND__;
typedef HWND__* HWND;

// On Linux/macOS, simple wrapper that manages the SWELL HWND
// SWELL creates its own top-level window using Win32 API emulation
class JsfxNativeWindow
{
public:
    JsfxNativeWindow(SX_Instance* instance, const juce::String& title, JsfxHelper& helper);
    ~JsfxNativeWindow();

    void setVisible(bool shouldBeVisible);
    void setAlwaysOnTop(bool shouldBeOnTop);

    // Callback for when I/O button is clicked (cross-platform)
    std::function<void()> onIOButtonClicked;

    // Allow access to helper for message handling
    JsfxHelper& jsfxHelper;

    // I/O button handler (called from static dialog proc)
    void handleJsfxIORequest(SX_Instance* jsfxInstance, HWND jsfxDialog);

private:
    SX_Instance* sxInstance = nullptr;
    void* nativeUIHandle = nullptr;     // HWND of JSFX child dialog
    void* parentWindowHandle = nullptr; // HWND of parent window

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxNativeWindow)
};
#endif
