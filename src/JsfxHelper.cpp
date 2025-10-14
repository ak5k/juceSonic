#include "JsfxHelper.h"

// JSFX and SWELL includes - isolated from JUCE code
#include <WDL/localize/localize.h>
#include <jsfx.h>

// JUCE includes for image processing
#include <juce_graphics/juce_graphics.h>

// g_hInst is defined in jsfx_api.cpp, we just use the extern declaration from sfxui.h

void JsfxHelper::initialize()
{
    // Get the module handle of the current DLL - required by JSFX system
    g_hInst = (HINSTANCE)juce::Process::getCurrentModuleInstanceHandle();

    // Initialize WDL localization system (required for JSFX UI dialogs)
    WDL_LoadLanguagePack("", NULL);
    DBG("JSFX Helper: WDL localization initialized");
}

void* JsfxHelper::createSliderBitmap(const void* data, int dataSize)
{
    // Create a memory stream from the binary data
    juce::MemoryInputStream stream(data, dataSize, false);

    // Load the image from the stream
    auto image = juce::ImageFileFormat::loadFrom(stream);
    if (!image.isValid())
        return nullptr;

    // Convert JUCE Image to Windows HBITMAP via SWELL
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
}

void JsfxHelper::setSliderBitmap(void* bitmap, bool isVertical)
{
    extern void Sliders_SetBitmap(HBITMAP hBitmap, bool isVert);
    Sliders_SetBitmap(static_cast<HBITMAP>(bitmap), isVertical);
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

#ifndef _WIN32
    // On non-Windows platforms, SWELL handles this via curses_ControlCreator
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
    if (!instance || !parentWindow)
        return nullptr;

    HWND parent = static_cast<HWND>(parentWindow);

    // Create JSFX UI - need to declare the function
    extern HWND sx_createUI(SX_Instance * instance, HINSTANCE hInst, HWND parent, void* hostctx);

    HWND uiWindow = sx_createUI(instance, g_hInst, parent, instance->m_hostctx);

    return uiWindow;
}

void JsfxHelper::destroyJsfxUI(SX_Instance* instance, void* uiHandle)
{
    if (!instance || !uiHandle)
        return;

    // Destroy JSFX UI - need to declare the function
    extern void sx_deleteUI(SX_Instance * instance);

    sx_deleteUI(instance);

    HWND hwnd = static_cast<HWND>(uiHandle);
    DestroyWindow(hwnd);
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