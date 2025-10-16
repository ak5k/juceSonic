/*
 * EmbeddedJsfxComponent - Platform-specific JSFX UI hosting
 * 
 * WINDOWS: JSFX UI is embedded as a child window using the JUCE window handle
 * MACOS:   JSFX UI is embedded as a child window using the JUCE window handle  
 * LINUX:   JSFX UI opens as an independent floating window (SWELL limitation)
 *          - Cannot be embedded in GTK hierarchy
 *          - Window is subclassed to prevent crash during destruction
 */

#include "EmbeddedJsfxComponent.h"

#include "JsfxHelper.h"

#include <jsfx/sfxui.h>

#ifndef _WIN32
#include <WDL/swell/swell.h>
extern HINSTANCE g_hInst; // Defined in jsfx_api.cpp

#ifdef __linux__
// Linux-specific: Subclass JSFX window to prevent crash during destruction
// JSFX tries to send WM_USER+1030 to parent during WM_DESTROY, but we have no parent
static WNDPROC g_originalJsfxProc = nullptr;

static LRESULT CALLBACK SafeJsfxWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY)
    {
        // Prevent attempt to notify NULL parent
        return 0;
    }
    
    if (g_originalJsfxProc)
        return CallWindowProc(g_originalJsfxProc, hwnd, msg, wParam, lParam);
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif
#endif



EmbeddedJsfxComponent::EmbeddedJsfxComponent(SX_Instance* instance, JsfxHelper& helper)
    : sxInstance(instance)
    , jsfxHelper(helper)
{
    // Make component transparent - the native window does all the painting
    setOpaque(false);

    // Try to create immediately after being added to hierarchy
    juce::MessageManager::callAsync(
        [this]()
        {
            if (!nativeCreated && isVisible())
            {
                createNative();
#ifndef __linux__
                if (!nativeCreated)
                {
                    // If immediate creation failed, start timer (Windows/Mac only)
                    createRetryCount = 0;
                    startTimer(50);
                    DBG("EmbeddedJsfxComponent: Constructor - starting timer to wait for parent window");
                }
#endif
            }
        }
    );
}

EmbeddedJsfxComponent::~EmbeddedJsfxComponent()
{
    stopTimer();
    destroyNative();
}





void EmbeddedJsfxComponent::createNative()
{
    if (nativeCreated || nativeUIHandle || !sxInstance)
    {
        if (nativeCreated)
            DBG("EmbeddedJsfxComponent: Native UI already created, skipping");
        return;
    }

#ifdef __linux__
    // On Linux, create as standalone floating window (no parent)
    // We'll handle the parent message issue in destroyNative
    DBG("EmbeddedJsfxComponent: Creating JSFX UI as floating window on Linux (no parent)");
    nativeUIHandle = jsfxHelper.createJsfxUI(sxInstance, nullptr);
#else
    // On Windows/Mac, get the native window handle and embed properly
    void* parentHandle = getWindowHandle();

    if (!parentHandle)
    {
        DBG("EmbeddedJsfxComponent: Parent window handle is null, cannot create native UI");
        return;
    }

    DBG("EmbeddedJsfxComponent: Creating JSFX UI with parent HWND: "
        + juce::String::toHexString((juce::pointer_sized_int)parentHandle));
    nativeUIHandle = jsfxHelper.createJsfxUI(sxInstance, parentHandle);
#endif
    if (!nativeUIHandle)
    {
        DBG("EmbeddedJsfxComponent: Failed to create JSFX UI");
        return;
    }

    nativeCreated = true;
    DBG("EmbeddedJsfxComponent: JSFX UI created successfully, child HWND: "
        + juce::String::toHexString((juce::pointer_sized_int)nativeUIHandle));

    // Get initial size and show the JSFX child window
    RECT r;
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(nativeUIHandle);
    GetClientRect(hwnd, &r);
    jsfxWindowWidth = r.right - r.left;
    jsfxWindowHeight = r.bottom - r.top;

    DBG("EmbeddedJsfxComponent: JSFX UI initial size: "
        + juce::String(jsfxWindowWidth)
        + "x"
        + juce::String(jsfxWindowHeight));

    // Notify parent about the native UI size
    if (onNativeCreated)
        onNativeCreated(jsfxWindowWidth, jsfxWindowHeight);

    // Position the child window at our component's position within the parent
    auto bounds = getLocalBounds();
    auto parentComp = getParentComponent();
    if (parentComp)
    {
        auto topLeft = parentComp->getLocalPoint(this, bounds.getTopLeft());
        SetWindowPos(hwnd, NULL, topLeft.getX(), topLeft.getY(), bounds.getWidth(), bounds.getHeight(), SWP_NOZORDER);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
#elif defined(__linux__)
    // Linux: standalone floating window with decorations
    HWND hwnd = static_cast<HWND>(nativeUIHandle);
    GetClientRect(hwnd, &r);
    jsfxWindowWidth = r.right - r.left;
    jsfxWindowHeight = r.bottom - r.top;

    DBG("EmbeddedJsfxComponent: JSFX UI initial size: "
        + juce::String(jsfxWindowWidth)
        + "x"
        + juce::String(jsfxWindowHeight));

    // Notify parent about the native UI size
    if (onNativeCreated)
        onNativeCreated(jsfxWindowWidth, jsfxWindowHeight);

    // Subclass the window to prevent crash during WM_DESTROY
    g_originalJsfxProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)SafeJsfxWindowProc);
    
    // Show the window explicitly - make sure it's not hidden
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    
    DBG("EmbeddedJsfxComponent: Linux JSFX window subclassed and shown");
#else
    // macOS: embedded in parent window
    HWND hwnd = static_cast<HWND>(nativeUIHandle);
    GetClientRect(hwnd, &r);
    jsfxWindowWidth = r.right - r.left;
    jsfxWindowHeight = r.bottom - r.top;

    DBG("EmbeddedJsfxComponent: JSFX UI initial size: "
        + juce::String(jsfxWindowWidth)
        + "x"
        + juce::String(jsfxWindowHeight));

    // Notify parent about the native UI size
    if (onNativeCreated)
        onNativeCreated(jsfxWindowWidth, jsfxWindowHeight);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
#endif
}

void EmbeddedJsfxComponent::destroyNative()
{
    if (!nativeCreated && !nativeUIHandle)
        return;

    DBG("EmbeddedJsfxComponent: Destroying native JSFX UI");

    if (nativeUIHandle)
    {
        HWND hwnd = static_cast<HWND>(nativeUIHandle);
        ShowWindow(hwnd, SW_HIDE);
        
        jsfxHelper.destroyJsfxUI(sxInstance, nativeUIHandle);
        nativeUIHandle = nullptr;
    }

    nativeCreated = false;
    DBG("EmbeddedJsfxComponent: Native JSFX UI destroyed");
}

void EmbeddedJsfxComponent::resized()
{
#ifndef __linux__
    // On Windows/Mac, reposition the embedded window
    // On Linux, window is independent floating window - no resizing needed
    if (nativeCreated && nativeUIHandle)
    {
        auto bounds = getLocalBounds();
        auto parentComp = getParentComponent();
        
        if (parentComp)
        {
            auto topLeft = parentComp->getLocalPoint(this, bounds.getTopLeft());
            
            HWND hwnd = static_cast<HWND>(nativeUIHandle);
            SetWindowPos(
                hwnd,
                NULL,
                topLeft.getX(),
                topLeft.getY(),
                bounds.getWidth(),
                bounds.getHeight(),
                SWP_NOZORDER
            );
        }
    }
#endif
}

void EmbeddedJsfxComponent::paint(juce::Graphics& g)
{
    // Don't paint anything - the native JSFX window handles all drawing
    // Component is transparent (opaque=false) so parent's background shows through
}

void EmbeddedJsfxComponent::visibilityChanged()
{
    if (isVisible() && !nativeCreated)
    {
#ifndef __linux__
        // Start a timer to retry creation until HWND is available (Windows/Mac only)
        createRetryCount = 0;
        startTimer(50);
        DBG("EmbeddedJsfxComponent: visibilityChanged - starting timer to wait for native peer");
#endif
    }
    else if (!isVisible() && nativeCreated && nativeUIHandle)
    {
        // Hide the native JSFX window
        DBG("EmbeddedJsfxComponent: visibilityChanged - hiding native JSFX window");
        HWND hwnd = static_cast<HWND>(nativeUIHandle);
        ShowWindow(hwnd, SW_HIDE);
    }
    else if (isVisible() && nativeCreated && nativeUIHandle)
    {
        // Show the native JSFX window again
        DBG("EmbeddedJsfxComponent: visibilityChanged - showing native JSFX window");
        HWND hwnd = static_cast<HWND>(nativeUIHandle);
        ShowWindow(hwnd, SW_SHOW);
    }
}

void EmbeddedJsfxComponent::timerCallback()
{
    // Only used on Windows/Mac for delayed window creation
    if (!isVisible() || nativeCreated)
    {
        stopTimer();
        return;
    }

#ifndef __linux__
    // Check if parent window handle is now available (Windows/Mac only)
    void* parentHandle = getWindowHandle();
    if (parentHandle != nullptr)
    {
        DBG("EmbeddedJsfxComponent: timerCallback - parent window handle now available, creating native UI");
        stopTimer();
        createNative();

        // Trigger resize to position the window correctly
        if (nativeCreated)
        {
            DBG("EmbeddedJsfxComponent: Native UI created successfully, triggering layout update");

            // Notify parent that our state changed and we need layout
            if (auto* parent = getParentComponent())
                parent->resized();

            resized();
            repaint();
        }
    }
    else
    {
        createRetryCount++;
        if (createRetryCount > 40) // Give up after 2 seconds
        {
            DBG("EmbeddedJsfxComponent: timerCallback - gave up waiting for parent window handle after "
                + juce::String(createRetryCount * 50)
                + "ms");
            stopTimer();
        }
        else
        {
            DBG("EmbeddedJsfxComponent: timerCallback - waiting for parent window handle (attempt "
                + juce::String(createRetryCount)
                + ")");
        }
    }
#endif
}
