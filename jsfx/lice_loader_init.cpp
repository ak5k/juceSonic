// Manual LICE image loader initialization
// This ensures the image loaders are properly registered even if static constructors don't run

#include "jsfx/WDL/lice/lice.h"
#include <cstring>

// External references to the loader objects defined in WDL
extern class LICE_PNGLoader LICE_pngldr; // from lice_png.cpp
extern class LICE_JPGLoader LICE_jgpldr; // from lice_jpg.cpp
extern class LICE_GIFLoader LICE_gifldr; // from lice_gif.cpp
extern struct _LICE_ImageLoader_rec* LICE_ImageLoader_list;

// Force symbol retention and early initialization
namespace
{
struct LICEInitializer
{
    LICEInitializer()
    {
        // Prevent dead code elimination of loader symbols
        static void* refs[] = {(void*)&LICE_pngldr, (void*)&LICE_jgpldr, (void*)&LICE_gifldr};
        volatile void* ptr = static_cast<volatile void*>(refs);
        (void)ptr;
    }
};

static LICEInitializer init;
} // namespace

// Fallback PNG loader registration
static void EnsurePNGLoader()
{
    // Check if PNG loader is registered
    for (auto* rec = LICE_ImageLoader_list; rec; rec = rec->_next)
        if (rec->get_extlist && strstr(rec->get_extlist(), "PNG"))
            return;

    // Manually register PNG loader if missing
    static struct _LICE_ImageLoader_rec png_rec;
    png_rec.loadfunc = [](const char* filename, bool checkFileName, LICE_IBitmap* bmpbase) -> LICE_IBitmap*
    {
        if (checkFileName)
        {
            const char* ext = strrchr(filename, '.');
            if (!ext || _stricmp(ext, ".png") != 0)
                return nullptr;
        }
        return LICE_LoadPNG(filename, bmpbase);
    };
    png_rec.get_extlist = []() -> const char* { return "PNG files (*.PNG)\0*.PNG\0"; };
    png_rec._next = LICE_ImageLoader_list;
    LICE_ImageLoader_list = &png_rec;
}

extern "C" void LICE_InitializeImageLoaders()
{
    static bool initialized = false;
    if (!initialized)
    {
        initialized = true;
        EnsurePNGLoader();
    }
}
