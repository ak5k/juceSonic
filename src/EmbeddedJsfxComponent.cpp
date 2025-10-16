#include "EmbeddedJsfxComponent.h"

#include "JsfxHelper.h"

#include <jsfx/sfxui.h>

EmbeddedJsfxComponent::EmbeddedJsfxComponent(SX_Instance* instance, JsfxHelper& helper)
    : sxInstance(instance)
    , jsfxHelper(helper)
{
    // Make component transparent - the native window does all the painting
    setOpaque(false);

    // We don't need HWNDComponent - we'll get the parent window handle directly
    // Try to create immediately after being added to hierarchy
    juce::MessageManager::callAsync(
        [this]()
        {
            if (!nativeCreated && isVisible())
            {
                createNative();
                if (!nativeCreated)
                {
                    // If immediate creation failed, start timer
                    createRetryCount = 0;
                    startTimer(50);
                    DBG("EmbeddedJsfxComponent: Constructor - starting timer to wait for parent window");
                }
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

    // Get the native window handle of this component's top-level window
    void* parentHandle = getWindowHandle();

    if (!parentHandle)
    {
        DBG("EmbeddedJsfxComponent: Parent window handle is null, cannot create native UI");
        return;
    }

    DBG("EmbeddedJsfxComponent: Creating JSFX UI with parent HWND: "
        + juce::String::toHexString((juce::pointer_sized_int)parentHandle));
    nativeUIHandle = jsfxHelper.createJsfxUI(sxInstance, parentHandle);
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
#else
    // SWELL: child dimensions (macOS/Linux)
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
#endif
}

void EmbeddedJsfxComponent::destroyNative()
{
    if (!nativeCreated && !nativeUIHandle)
        return;

    DBG("EmbeddedJsfxComponent: Destroying native JSFX UI");

    if (nativeUIHandle)
    {
        // Hide the window before destroying
#ifdef _WIN32
        HWND hwnd = static_cast<HWND>(nativeUIHandle);
        ShowWindow(hwnd, SW_HIDE);
#else
        HWND hwnd = static_cast<HWND>(nativeUIHandle);
        ShowWindow(hwnd, SW_HIDE);
#endif

        jsfxHelper.destroyJsfxUI(sxInstance, nativeUIHandle);
        nativeUIHandle = nullptr;
    }

    nativeCreated = false;
    DBG("EmbeddedJsfxComponent: Native JSFX UI destroyed");
}

void EmbeddedJsfxComponent::resized()
{
    if (nativeCreated && nativeUIHandle)
    {
        auto bounds = getLocalBounds();
        auto parentComp = getParentComponent();
        if (parentComp)
        {
            // Get position relative to parent window
            auto topLeft = parentComp->getLocalPoint(this, bounds.getTopLeft());

#ifdef _WIN32
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
            DBG("EmbeddedJsfxComponent: Resized to "
                + juce::String(bounds.getWidth())
                + "x"
                + juce::String(bounds.getHeight())
                + " at ("
                + juce::String(topLeft.getX())
                + ","
                + juce::String(topLeft.getY())
                + ")");
#else
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
#endif
        }
    }
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
        // Start a timer to retry creation until HWND is available
        createRetryCount = 0;
        startTimer(50); // Check every 50ms
        DBG("EmbeddedJsfxComponent: visibilityChanged - starting timer to wait for native peer");
    }
    else if (!isVisible() && nativeCreated && nativeUIHandle)
    {
        // Hide the native JSFX window
        DBG("EmbeddedJsfxComponent: visibilityChanged - hiding native JSFX window");
#ifdef _WIN32
        HWND hwnd = static_cast<HWND>(nativeUIHandle);
        ShowWindow(hwnd, SW_HIDE);
#else
        HWND hwnd = static_cast<HWND>(nativeUIHandle);
        ShowWindow(hwnd, SW_HIDE);
#endif
    }
    else if (isVisible() && nativeCreated && nativeUIHandle)
    {
        // Show the native JSFX window again
        DBG("EmbeddedJsfxComponent: visibilityChanged - showing native JSFX window");
#ifdef _WIN32
        HWND hwnd = static_cast<HWND>(nativeUIHandle);
        ShowWindow(hwnd, SW_SHOW);
#else
        HWND hwnd = static_cast<HWND>(nativeUIHandle);
        ShowWindow(hwnd, SW_SHOW);
#endif
    }
}

void EmbeddedJsfxComponent::timerCallback()
{
    if (!isVisible() || nativeCreated)
    {
        stopTimer();
        return;
    }

    // Check if parent window handle is now available
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
}
