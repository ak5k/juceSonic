#pragma once

#include <juce_core/juce_core.h>

// Forward declaration to avoid exposing JSFX types in JUCE code
struct SX_Instance;

/**
 * Helper class to isolate JSFX-specific code that uses Win32 API/SWELL
 * from the main JUCE plugin code. This keeps the plugin code clean
 * and allows the JSFX integration to be platform-specific.
 */
class JsfxHelper
{
public:
    // Initialize JSFX system (localization, controls, bitmaps)
    static void initialize();

    // Register custom JSFX window classes
    static void registerJsfxWindowClasses();

    // UI Management - isolates Win32/SWELL from JUCE
    struct UISize
    {
        int width, height;
    };

    // Create JSFX UI and return platform-specific handle
    static void* createJsfxUI(SX_Instance* instance, void* parentWindow);

    // Destroy JSFX UI
    static void destroyJsfxUI(SX_Instance* instance, void* uiHandle);

    // Get the natural size of the created UI
    static UISize getJsfxUISize(void* uiHandle);

    // Position the UI within its parent
    static void positionJsfxUI(void* uiHandle, int x, int y, int width, int height);

    // Show/hide the UI
    static void showJsfxUI(void* uiHandle, bool show);

    // Cleanup resources
    static void cleanup();
};