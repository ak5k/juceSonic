#pragma once

#include <atomic>
#include <juce_core/juce_core.h>

// Forward declaration to avoid exposing JSFX types in JUCE code
struct SX_Instance;

/**
 * Helper class to isolate JSFX-specific code that uses Win32 API/SWELL
 * from the main JUCE plugin code. This keeps the plugin code clean
 * and allows the JSFX integration to be platform-specific.
 *
 * Each plugin instance inherits from this class to get its own
 * JSFX system resources, avoiding global/static shared state.
 */
class JsfxHelper
{
public:
    JsfxHelper();
    virtual ~JsfxHelper();

    // UI Management - isolates Win32/SWELL from JUCE
    struct UISize
    {
        int width, height;
    };

    // Create JSFX UI and return platform-specific handle
    void* createJsfxUI(SX_Instance* instance, void* parentWindow);

    // Destroy JSFX UI
    void destroyJsfxUI(SX_Instance* instance, void* uiHandle);

    // Get the natural size of the created UI
    static UISize getJsfxUISize(void* uiHandle);

    // Position the UI within its parent
    static void positionJsfxUI(void* uiHandle, int x, int y, int width, int height);

    // Show/hide the UI
    static void showJsfxUI(void* uiHandle, bool show);

    // Host callback functions for JSFX
    static int hostGetSetNumChannels(void* hostctx, int* numChannels);
    static int
    hostGetSetPinMap2(void* hostctx, bool isOutput, unsigned int* mapping, int channelOffset, int* isSetSize);
    static int hostGetSetPinmapperFlags(void* hostctx, int* flags);

    // API function getter for JSFX to retrieve host callbacks
    static void* getHostAPIFunction(const char* functionName);

protected:
    // Initialize JSFX system for this instance
    void initializeJsfxSystem();

    // Cleanup JSFX system for this instance
    void cleanupJsfxSystem();

    // Register custom JSFX window classes (shared resource)
    static void registerJsfxWindowClasses();

private:
    // Per-instance initialization state
    bool m_jsfxInitialized = false;

    // Per-instance JSFX resources
    void* m_sliderBitmap = nullptr;

    // Reference counting for shared resources (window classes, etc.)
    static std::atomic<int> s_instanceCount;

    // Initialize/cleanup shared resources (window classes, etc.)
    static void initializeSharedResources();
    static void cleanupSharedResources();
};