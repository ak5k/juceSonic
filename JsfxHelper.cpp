#include "JsfxHelper.h"

// JSFX and SWELL includes - isolated from JUCE code
#include "build/_deps/jsfx-src/WDL/localize/localize.h"
#include "build/_deps/jsfx-src/WDL/swell/swell.h"
#include "build/_deps/jsfx-src/jsfx/sfxui.h"

// JUCE includes for image processing
#include <juce_graphics/juce_graphics.h>

// Global variables for JSFX system
static HINSTANCE g_hInst = nullptr;

void JsfxHelper::initialize()
{
    // Get the module handle of the current DLL
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

    // Register WDLCursesWindow class (debug window)
    wc.lpszClassName = "WDLCursesWindow";
    RegisterClassA(&wc);

    DBG("JSFX Helper: Window classes registered");
}

void JsfxHelper::cleanup()
{
    // Cleanup can be implemented here if needed
    DBG("JSFX Helper: Cleanup completed");
}