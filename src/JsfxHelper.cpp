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

// Forward declare standalone-helper initialization functions
extern void Sliders_Init(HINSTANCE hInst, bool reg, int hslider_bitmap_id);
extern void Meters_Init(HINSTANCE hInst, bool reg);

void JsfxHelper::initialize()
{
    // Get the module handle - required by JSFX system
#ifdef _WIN32
    g_hInst = (HINSTANCE)juce::Process::getCurrentModuleInstanceHandle();

    // Initialize WDL localization system (Windows only)
    WDL_LoadLanguagePack("", NULL);
    DBG("JSFX Helper: WDL localization initialized");
#else
    // On Linux/Mac with SWELL, initialize SWELL subsystems
    // JUCE has already initialized GTK, so we just initialize SWELL's message handling
    extern void SWELL_Internal_PostMessage_Init();
    SWELL_Internal_PostMessage_Init();

    // Set an application name for SWELL
    extern void* SWELL_ExtendedAPI(const char* key, void* value);
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
}

void* JsfxHelper::createSliderBitmap(const void* data, int dataSize)
{
#ifdef _WIN32
    // Create a memory stream from the binary data
    juce::MemoryInputStream stream(data, dataSize, false);

    // Load the image from the stream
    auto image = juce::ImageFileFormat::loadFrom(stream);
    if (!image.isValid())
        return nullptr;

    // Convert JUCE Image to Windows HBITMAP
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = image.getWidth();
    bmi.bmiHeader.biHeight = -image.getHeight(); // Negative for top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bitmapData;
    HBITMAP hBitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bitmapData, nullptr, 0);

    if (hBitmap && bitmapData)
    {
        // Copy pixel data from JUCE Image to bitmap
        juce::Image::BitmapData imgData(image, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < image.getHeight(); ++y)
        {
            uint32_t* destRow = static_cast<uint32_t*>(bitmapData) + (y * image.getWidth());
            for (int x = 0; x < image.getWidth(); ++x)
            {
                juce::Colour pixel = imgData.getPixelColour(x, y);
                // Convert ARGB to BGRA for Windows bitmap
                destRow[x] =
                    (pixel.getAlpha() << 24) | (pixel.getRed() << 16) | (pixel.getGreen() << 8) | pixel.getBlue();
            }
        }
    }

    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    return hBitmap;
#else
    // SWELL doesn't support CreateDIBSection, and slider bitmaps are optional
    // JSFX will use default rendering if no bitmaps are provided
    (void)data;
    (void)dataSize;
    DBG("JSFX Helper: Slider bitmaps not supported on this platform - using defaults");
    return nullptr;
#endif
}

void JsfxHelper::setSliderBitmap(void* bitmap, bool isVertical)
{
#ifdef _WIN32
    extern void Sliders_SetBitmap(HBITMAP hBitmap, bool isVert);
    Sliders_SetBitmap(static_cast<HBITMAP>(bitmap), isVertical);
#else
    (void)bitmap;
    (void)isVertical;
#endif
}

void JsfxHelper::initializeSliders(void* moduleHandle, bool registerControls, int bitmapId)
{
    extern void Sliders_Init(HINSTANCE hInst, bool reg, int hslider_bitmap_id);
    Sliders_Init(static_cast<HINSTANCE>(moduleHandle), registerControls, bitmapId);
}

void JsfxHelper::initializeMeters(void* moduleHandle, bool registerControls)
{
    extern void Meters_Init(HINSTANCE hInst, bool reg);
    Meters_Init(static_cast<HINSTANCE>(moduleHandle), registerControls);
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
    extern void curses_registerChildClass(HINSTANCE hInstance);
    curses_registerChildClass(g_hInst);
#else
    // On non-Windows platforms with SWELL, custom control registration is handled automatically
    // SWELL uses control creators instead of RegisterClass
    extern HWND curses_ControlCreator(
        HWND parent,
        const char* cname,
        int idx,
        const char* classname,
        int style,
        int x,
        int y,
        int w,
        int h
    );
    // TODO: Register curses_ControlCreator with SWELL
#endif

    DBG("JSFX Helper: Window classes registered (including WDL curses)");
}

void* JsfxHelper::createJsfxUI(SX_Instance* instance, void* parentWindow)
{
    if (!instance)
        return nullptr;

    // sx_createUI creates a child window that needs a SWELL parent window
    // On Linux, parentWindow should be a SWELL HWND, not a raw GTK widget
    extern HWND sx_createUI(SX_Instance * instance, HINSTANCE hInst, HWND parent, void* hostctx);

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
    extern void sx_deleteUI(SX_Instance * instance);

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
    extern void Sliders_Init(HINSTANCE hInst, bool reg, int hslider_bitmap_id);
    extern void Meters_Init(HINSTANCE hInst, bool reg);

    Sliders_Init(g_hInst, false, 0); // Unregister sliders
    Meters_Init(g_hInst, false);     // Unregister meters

    // Unregister WDL curses window class
    extern void curses_unregisterChildClass(HINSTANCE hInstance);
    curses_unregisterChildClass(g_hInst);

    DBG("JSFX Helper: Cleanup completed (unregistered curses class, sliders, and meters)");
}