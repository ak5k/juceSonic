#include "JsfxHelper.h"

// Forward declaration to avoid circular dependency
class AudioPluginAudioProcessor;

// JSFX and SWELL includes - isolated from JUCE code
#ifdef _WIN32
// Include localize.h on Windows only (uses Windows-specific types)
#include <WDL/localize/localize.h>
#endif
#include <jsfx.h>

// External declaration for JSFX API (defined in PluginProcessor.cpp)
extern jsfxAPI JesusonicAPI;

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

// Static member initialization
std::atomic<int> JsfxHelper::s_instanceCount{0};

JsfxHelper::JsfxHelper()
{
    // Initialize shared resources on first instance
    initializeSharedResources();

    // Initialize per-instance JSFX system
    initializeJsfxSystem();
}

JsfxHelper::~JsfxHelper()
{
    // Cleanup per-instance resources
    cleanupJsfxSystem();

    // Cleanup shared resources when last instance is destroyed
    cleanupSharedResources();
}

void JsfxHelper::initializeSharedResources()
{
    int currentCount = s_instanceCount.fetch_add(1);
    if (currentCount > 0)
    {
        // Shared resources already initialized by another instance

        return;
    }



    // Get the module handle - required by JSFX system
#ifdef _WIN32
    g_hInst = (HINSTANCE)juce::Process::getCurrentModuleInstanceHandle();

    if (!g_hInst)
    {

        return;
    }

    // Initialize WDL localization system (Windows only)
    WDL_LoadLanguagePack("", NULL);

#else
    // On Linux/Mac with SWELL, initialize SWELL subsystems
    // JUCE has already initialized GTK, so we just initialize SWELL's message handling
    SWELL_Internal_PostMessage_Init();

    // Set an application name for SWELL
    SWELL_ExtendedAPI("APPNAME", (void*)JucePlugin_Name);

    // On Linux/Mac with SWELL, just use a dummy instance handle
    g_hInst = (HINSTANCE)1;

#endif

    // Register host API function getter with JSFX
    // This allows JSFX to retrieve host callbacks like pin mapping
    extern void sx_provideAPIFunctionGetter(void* (*getFunc)(const char*));
    sx_provideAPIFunctionGetter(&getHostAPIFunction);


    // Register JSFX window classes
    registerJsfxWindowClasses();
}

void JsfxHelper::cleanupSharedResources()
{
    int currentCount = s_instanceCount.fetch_sub(1);
    if (currentCount > 1)
    {
        // Still have other instances using shared resources

        return;
    }



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


}

void JsfxHelper::initializeJsfxSystem()
{
    if (m_jsfxInitialized)
        return;



    // Initialize JSFX standalone controls (sliders and VU meters) for this instance
    // These register custom control creators with SWELL on non-Windows platforms
    Sliders_Init(g_hInst, true, 0); // reg=true to register, bitmap_id=0 (no resource bitmap)
    Meters_Init(g_hInst, true);     // reg=true to register

    // Create a per-instance slider thumb bitmap using JUCE
    static const int thumbWidth = 23;
    static const int thumbHeight = 14;

    // Create instance-specific image
    juce::Image thumbImage(juce::Image::ARGB, thumbWidth, thumbHeight, true);

    // Draw the bitmap for this instance
    {
        // Draw into the image using Graphics - scope this block to ensure Graphics is destroyed
        // before we create BitmapData (Direct2D only allows one context at a time)
        {
            juce::Graphics graphics(thumbImage);

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



        // Create bitmap using Win32/SWELL CreateBitmap API
        // On Windows: standard Win32 CreateBitmap
        // On Linux/Mac: SWELL's CreateBitmap (requires 32-bit ARGB format)
        juce::Image::BitmapData bitmap(thumbImage, juce::Image::BitmapData::readOnly);

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
        m_sliderBitmap = CreateBitmap(thumbWidth, thumbHeight, 1, 32, bitmapBits.data());

        if (m_sliderBitmap)
        {

        }
        else
        {

            return;
        }
    }

    if (m_sliderBitmap)
    {
        Sliders_SetBitmap(static_cast<HBITMAP>(m_sliderBitmap), false);

    }
    else
    {

    }

    m_jsfxInitialized = true;

}

void JsfxHelper::cleanupJsfxSystem()
{
    if (!m_jsfxInitialized)
        return;



    // Clean up per-instance bitmap
    if (m_sliderBitmap)
    {
        DeleteObject(static_cast<HBITMAP>(m_sliderBitmap));
        m_sliderBitmap = nullptr;
    }

    m_jsfxInitialized = false;
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
    // SWELL_RegisterCustomControlCreator in sx_provideAPIFunctionGetter
    // JSFX will call SWELL_RegisterCustomControlCreator(curses_ControlCreator) itself
    // when it calls our getHostAPIFunction with "Mac_CustomControlCreator"

#endif


}

void* JsfxHelper::createJsfxUI(SX_Instance* instance, void* parentWindow)
{
    if (!instance)
        return nullptr;

    // Set the host context to the instance itself
    instance->m_hostctx = instance;

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

// Host callback: Get/set number of channels
// Called by JSFX to query how many channels the host supports
int JsfxHelper::hostGetSetNumChannels(void* hostctx, int* numChannels)
{
    if (!hostctx || !numChannels)
        return 0;

    // hostctx is a pointer to AudioPluginAudioProcessor (set via sx_set_host_ctx)
    // Forward declare to avoid circular dependency
    class AudioPluginAudioProcessor;

    // JSFX is querying the total number of input channels available
    // Return total input channels (main + sidechain)
    // We need to get this from the processor's bus layout
    // For now, return the value as-is since we don't have access to processor methods here

    return *numChannels; // Return current value
}

// Host callback: Get/set pin mapping
// This allows JSFX to query/set which physical channels map to which logical pins
int JsfxHelper::hostGetSetPinMap2(
    void* hostctx,
    bool isOutput,
    unsigned int* mapping,
    int channelOffset,
    int* isSetSize
)
{
    if (!hostctx)
        return 0;

    // hostctx should be a pointer to AudioPluginAudioProcessor
    // mapping: array of channel mappings (each uint is a bitmask of physical channels)
    // channelOffset: starting channel for this mapping block
    // isSetSize: if not null, this is a set operation (set the mapping)

    // For now, implement simple 1:1 mapping (pin 0 -> channel 0, pin 1 -> channel 1, etc.)
    // This means each JSFX pin maps directly to one physical channel

    if (!isSetSize)
    {
        // Query operation - return current mapping
        if (mapping)
        {
            // Simple 1:1 mapping: each pin gets one channel
            for (int i = 0; i < 64; ++i)                  // Support up to 64 channels
                mapping[i] = (1u << (channelOffset + i)); // Bit mask for single channel
        }
        return 64; // Return number of mappings available
    }
    else
    {
        // Set operation - JSFX wants to change the mapping
        // For now, we don't support dynamic remapping
        return 0; // Return 0 to indicate we don't support setting
    }
}

// Host callback: Get/set pin mapper flags
// Returns flags that control pin mapper behavior
int JsfxHelper::hostGetSetPinmapperFlags(void* hostctx, int* flags)
{
    if (!hostctx)
        return 0;

    if (flags)
    {
        // Return flags indicating pin mapper capabilities
        // For now, return 0 (no special flags)
        *flags = 0;
    }

    return 1; // Return 1 to indicate success
}

// API function getter callback
// JSFX calls this to get pointers to host-provided functions
void* JsfxHelper::getHostAPIFunction(const char* functionName)
{
    if (!functionName)
        return nullptr;



    // Return function pointers for the callbacks JSFX needs
    if (strcmp(functionName, "fxGetSetHostNumChan") == 0)
        return (void*)&hostGetSetNumChannels;

    if (strcmp(functionName, "fxGetSetPinMap2") == 0)
        return (void*)&hostGetSetPinMap2;

    if (strcmp(functionName, "fxGetSetPinmapperFlags") == 0)
        return (void*)&hostGetSetPinmapperFlags;

#ifndef _WIN32
    // On Mac/Linux with SWELL, JSFX requests Mac_CustomControlCreator
    // We provide the curses_ControlCreator instead (already declared as extern)
    if (strcmp(functionName, "Mac_CustomControlCreator") == 0)
    {
        extern HWND curses_ControlCreator(HWND, const char*, int, const char*, int, int, int, int, int);
        return (void*)curses_ControlCreator;
    }
#endif

    // Return nullptr for functions we don't implement
    return nullptr;
}

juce::String JsfxHelper::parseJSFXAuthor(const juce::File& jsfxFile)
{
    if (!jsfxFile.existsAsFile())
        return "Unknown";

    juce::FileInputStream stream(jsfxFile);
    if (!stream.openedOk())
        return "Unknown";

    // Read file line by line looking for "author:" tag
    juce::String line;
    while (!stream.isExhausted())
    {
        line = stream.readNextLine();

        // Check if line starts with "author:" (case insensitive)
        if (line.trimStart().startsWithIgnoreCase("author:"))
        {
            // Extract everything after "author:" and trim whitespace
            auto authorStart = line.indexOf(":");
            if (authorStart >= 0)
            {
                juce::String author = line.substring(authorStart + 1).trim();
                if (author.isNotEmpty())
                    return author;
            }
        }

        // Stop reading after first code section starts (optimization)
        if (line.trimStart().startsWith("@"))
            break;
    }

    return "Unknown";
}
