// Manual LICE image loader initialization
// This ensures the image loaders are properly registered even if static constructors don't run

#include "jsfx/WDL/lice/lice.h"

// External references to the loader objects defined in WDL
// These are global static objects whose constructors register themselves
extern class LICE_PNGLoader LICE_pngldr; // from lice_png.cpp
extern class LICE_JPGLoader LICE_jgpldr; // from lice_jpg.cpp
extern class LICE_GIFLoader LICE_gifldr; // from lice_gif.cpp

// Manual initialization function
extern "C" void LICE_InitializeImageLoaders()
{
    // Force the linker to include the loader object files by referencing their global objects
    // The constructors of these objects automatically register them in LICE_ImageLoader_list
    static bool initialized = false;
    if (!initialized)
    {
        initialized = true;

        // Reference the global loader objects to force linking
        // This ensures their static constructors run
        volatile void* png_loader = (void*)&LICE_pngldr;
        volatile void* jpg_loader = (void*)&LICE_jgpldr;
        volatile void* gif_loader = (void*)&LICE_gifldr;

        (void)png_loader;
        (void)jpg_loader;
        (void)gif_loader;
    }
}
