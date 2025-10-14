#include "JsfxHelper.h"

// JSFX and SWELL includes - isolated from JUCE code
#ifdef _WIN32
// Include localize.h on Windows only (uses Windows-specific types)
#include <WDL/localize/localize.h>
#endif
#include <jsfx.h>

// JUCE includes for image processing
#include <juce_graphics/juce_graphics.h>

// g_hInst is defined in jsfx_api.cpp, we just use the extern declaration from sfxui.h

// Forward declarations for JSFX/WDL functions used throughout this file
extern void Sliders_Init(HINSTANCE hInst, bool reg, int hslider_bitmap_id);
extern void Meters_Init(HINSTANCE hInst, bool reg);
extern void Sliders_SetBitmap(HBITMAP hBitmap, bool isVert);
extern HWND sx_createUI(SX_Instance* instance, HINSTANCE hInst, HWND parent, void* hostctx);
extern void sx_deleteUI(SX_Instance* instance);

#ifndef _WIN32
// SWELL-specific functions (non-Windows platforms)
extern void SWELL_Internal_PostMessage_Init();
extern void* SWELL_ExtendedAPI(const char* key, void* value);
extern HWND curses_ControlCreator(HWND, const char*, int, const char*, int, int, int, int, int);
extern void SWELL_RegisterCustomControlCreator(HWND (*)(HWND, const char*, int, const char*, int, int, int, int, int));
extern void
    SWELL_UnregisterCustomControlCreator(HWND (*)(HWND, const char*, int, const char*, int, int, int, int, int));
#else
// Windows-specific functions
extern void curses_registerChildClass(HINSTANCE hInstance);
extern void curses_unregisterChildClass(HINSTANCE hInstance);
#endif

void JsfxHelper::initialize()
{
    // Get the module handle - required by JSFX system
#ifdef _WIN32
    g_hInst = (HINSTANCE)juce::Process::getCurrentModuleInstanceHandle();

    if (!g_hInst)
    {
        DBG("JSFX Helper: ERROR - Failed to get module instance handle");
        return;
    }

    // Initialize WDL localization system (Windows only)
    WDL_LoadLanguagePack("", NULL);
    DBG("JSFX Helper: WDL localization initialized");
#else
    // On Linux/Mac with SWELL, initialize SWELL subsystems
    // JUCE has already initialized GTK, so we just initialize SWELL's message handling
    SWELL_Internal_PostMessage_Init();

    // Set an application name for SWELL
    SWELL_ExtendedAPI("APPNAME", (void*)"juceSonic");

    // On Linux/Mac with SWELL, just use a dummy instance handle
    g_hInst = (HINSTANCE)1;
    DBG("JSFX Helper: SWELL initialized (PostMessage and AppName)");
#endif

    // Initialize JSFX standalone controls (sliders and VU meters)
    // These register custom control creators with SWELL on non-Windows platforms
    Sliders_Init(g_hInst, true, 0); // reg=true to register, bitmap_id=0 (no resource bitmap)
    Meters_Init(g_hInst, true);     // reg=true to register

    DBG("JSFX Helper: Standalone controls initialized (sliders and meters)");

    // Create a cross-platform slider thumb bitmap using JUCE
    // Store it globally and use Win32/SWELL API to create the bitmap

    // Create a simple gray slider thumb using JUCE graphics
    // Original cockos_hslider.bmp was 23x14 pixels
    static const int thumbWidth = 23;
    static const int thumbHeight = 14;

    // Global image that persists for the lifetime of the application
    static juce::Image globalThumbImage(juce::Image::ARGB, thumbWidth, thumbHeight, true);
    static HBITMAP globalSliderBitmap = nullptr;

    if (!globalSliderBitmap)
    {
        // Draw into the image using Graphics - scope this block to ensure Graphics is destroyed
        // before we create BitmapData (Direct2D only allows one context at a time)
        {
            juce::Graphics graphics(globalThumbImage);

            // Fill entire image with opaque background color to avoid alpha channel issues on Windows
            graphics.fillAll(juce::Colour(0xffc0c0c0)); // Light gray, fully opaque

            // Draw the main thumb body with rounded corners
            graphics.setColour(juce::Colour(0xff909090)); // Medium gray
            graphics.fillRoundedRectangle(1.0f, 1.0f, thumbWidth - 2.0f, thumbHeight - 2.0f, 2.0f);

            // Add a subtle highlight on top for 3D effect
            graphics.setColour(juce::Colour(0xffb0b0b0)); // Lighter gray
            graphics.fillRoundedRectangle(2.0f, 2.0f, thumbWidth - 4.0f, thumbHeight / 2.0f - 1.0f, 1.5f);

            // Add a darker border for definition
            graphics.setColour(juce::Colour(0xff707070)); // Dark gray
            graphics.drawRoundedRectangle(1.0f, 1.0f, thumbWidth - 2.0f, thumbHeight - 2.0f, 2.0f, 1.0f);
        } // Graphics context destroyed here

        DBG("JSFX Helper: Created slider thumb image in memory");

        // Create bitmap using Win32/SWELL CreateBitmap API
        // On Windows: standard Win32 CreateBitmap
        // On Linux/Mac: SWELL's CreateBitmap (requires 32-bit ARGB format)
        juce::Image::BitmapData bitmap(globalThumbImage, juce::Image::BitmapData::readOnly);

        // Allocate memory for bitmap bits (32-bit ARGB/BGRA)
        std::vector<uint8_t> bitmapBits(thumbWidth * thumbHeight * 4);

        for (int y = 0; y < thumbHeight; ++y)
        {
            for (int x = 0; x < thumbWidth; ++x)
            {
                const uint8_t* pixel = bitmap.getPixelPointer(x, y);
                uint8_t r = pixel[0];
                uint8_t g = pixel[1];
                uint8_t b = pixel[2];
                uint8_t a = pixel[3];

                // Win32/SWELL expects BGRA format (4 bytes per pixel)
                int idx = (y * thumbWidth + x) * 4;
                bitmapBits[idx + 0] = b; // Blue
                bitmapBits[idx + 1] = g; // Green
                bitmapBits[idx + 2] = r; // Red
                bitmapBits[idx + 3] = a; // Alpha
            }
        }

        // Create the bitmap with CreateBitmap (32-bit for both Windows and SWELL)
        globalSliderBitmap = CreateBitmap(thumbWidth, thumbHeight, 1, 32, bitmapBits.data());

        if (globalSliderBitmap)
        {
            DBG("JSFX Helper: Created cross-platform slider thumb bitmap using Win32/SWELL API");
        }
        else
        {
            DBG("JSFX Helper: ERROR - CreateBitmap failed for slider thumb");
            return;
        }
    }

    if (globalSliderBitmap)
    {
        Sliders_SetBitmap(globalSliderBitmap, false);
        DBG("JSFX Helper: Slider bitmap registered with controls");
    }
    else
    {
        DBG("JSFX Helper: ERROR - No slider bitmap available to register");
    }
}

void JsfxHelper::registerJsfxWindowClasses()
{
#ifdef _WIN32
    // Register the custom JSFX slider control class
    WNDCLASSA wc = {};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    // Register REAPERknob class (knob control)
    wc.lpszClassName = "REAPERknob";
    RegisterClassA(&wc);

    // Register REAPERvertvu class (VU meter)
    wc.lpszClassName = "REAPERvertvu";
    RegisterClassA(&wc);

    // Register WDLCursesWindow class using proper curses registration
    // The WDL curses system has its own registration function
    curses_registerChildClass(g_hInst);
#else
    // On non-Windows platforms with SWELL, custom control registration is handled via
    // SWELL_RegisterCustomControlCreator
    // Register the curses control creator with SWELL so it can create WDLCursesWindow controls
    SWELL_RegisterCustomControlCreator(curses_ControlCreator);
    DBG("JSFX Helper: Registered curses control creator with SWELL");
#endif

    DBG("JSFX Helper: Window classes registered (including WDL curses)");
}

void* JsfxHelper::createJsfxUI(SX_Instance* instance, void* parentWindow)
{
    if (!instance)
        return nullptr;

    // sx_createUI creates a child window that needs a SWELL parent window
    // On Linux, parentWindow should be a SWELL HWND, not a raw GTK widget
    HWND parent = static_cast<HWND>(parentWindow);
    HWND uiWindow = sx_createUI(instance, g_hInst, parent, instance->m_hostctx);

    return uiWindow;
}

void JsfxHelper::destroyJsfxUI(SX_Instance* instance, void* uiHandle)
{
    if (!instance || !uiHandle)
        return;

    // Destroy JSFX UI - sx_deleteUI handles the window destruction internally
    // so we don't need to call DestroyWindow separately
    sx_deleteUI(instance);

    // Note: sx_deleteUI already calls DestroyWindow on the UI window,
    // so we don't need to (and shouldn't) call it again here
}

JsfxHelper::UISize JsfxHelper::getJsfxUISize(void* uiHandle)
{
    if (!uiHandle)
        return {0, 0};

    HWND hwnd = static_cast<HWND>(uiHandle);
    RECT rect;
    GetWindowRect(hwnd, &rect);

    return {rect.right - rect.left, rect.bottom - rect.top};
}

void JsfxHelper::positionJsfxUI(void* uiHandle, int x, int y, int width, int height)
{
    if (!uiHandle)
        return;

    HWND hwnd = static_cast<HWND>(uiHandle);
    SetWindowPos(hwnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void JsfxHelper::showJsfxUI(void* uiHandle, bool show)
{
    if (!uiHandle)
        return;

    HWND hwnd = static_cast<HWND>(uiHandle);
    ShowWindow(hwnd, show ? SW_SHOWNA : SW_HIDE);
}

void JsfxHelper::cleanup()
{
    // Unregister JSFX controls
    Sliders_Init(g_hInst, false, 0); // Unregister sliders
    Meters_Init(g_hInst, false);     // Unregister meters

    // Unregister WDL curses window class (platform-specific)
#ifdef _WIN32
    curses_unregisterChildClass(g_hInst);
#else
    // On non-Windows, unregister the custom control creator we registered
    SWELL_UnregisterCustomControlCreator(curses_ControlCreator);
#endif

    DBG("JSFX Helper: Cleanup completed (unregistered curses class, sliders, and meters)");
}