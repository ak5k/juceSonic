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

    // Create HBITMAP from JUCE binary data for slider controls
    // Returns platform-specific handle that can be used with JSFX
    static void* createSliderBitmap(const void* data, int dataSize);

    // Set up slider bitmap for JSFX controls
    static void setSliderBitmap(void* bitmap, bool isVertical);

    // Initialize JSFX sliders with platform-specific instance handle
    static void initializeSliders(void* moduleHandle, bool registerControls, int bitmapId);

    // Register custom JSFX window classes
    static void registerJsfxWindowClasses();

    // Cleanup resources
    static void cleanup();
};