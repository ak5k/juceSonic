/*
 * JSFX GFX API - Public interface for accessing JSFX graphics state
 *
 * This header provides a clean C API for accessing JSFX gfx framebuffer
 * without exposing internal WDL/EEL headers to users.
 */

#ifndef JSFX_GFX_API_H
#define JSFX_GFX_API_H

#ifdef __cplusplus
extern "C"
{
#endif

    // Opaque handle types
    typedef struct SX_Instance SX_Instance;
    typedef struct LICE_IBitmap LICE_IBitmap;

    // Get the LICE framebuffer from a JSFX instance
    // Returns NULL if no gfx state exists
    LICE_IBitmap* jsfx_get_framebuffer(SX_Instance* instance);

    // Get framebuffer dimensions
    void jsfx_get_gfx_dim(SX_Instance* instance, int* width, int* height);

    // Set gfx.w and gfx.h variables
    void jsfx_set_gfx_dim(SX_Instance* instance, int width, int height);

    // Get mouse state pointers (for setting from external UI)
    double* jsfx_get_mouse_x(SX_Instance* instance);
    double* jsfx_get_mouse_y(SX_Instance* instance);
    double* jsfx_get_mouse_cap(SX_Instance* instance);
    double* jsfx_get_mouse_wheel(SX_Instance* instance);
    double* jsfx_get_mouse_hwheel(SX_Instance* instance);

    // Check and clear framebuffer dirty flag
    int jsfx_is_framebuffer_dirty(SX_Instance* instance);
    void jsfx_clear_framebuffer_dirty(SX_Instance* instance);

    // LICE bitmap accessors (to avoid including lice.h)
    void* jsfx_lice_get_bits(LICE_IBitmap* bm);
    int jsfx_lice_get_width(LICE_IBitmap* bm);
    int jsfx_lice_get_height(LICE_IBitmap* bm);
    int jsfx_lice_get_rowspan(LICE_IBitmap* bm);
    void jsfx_lice_resize(LICE_IBitmap* bm, int w, int h);

    // LICE pixel format accessors (LICE uses 32-bit ARGB pixels)
    unsigned char jsfx_lice_get_r(unsigned int pixel);
    unsigned char jsfx_lice_get_g(unsigned int pixel);
    unsigned char jsfx_lice_get_b(unsigned int pixel);
    unsigned char jsfx_lice_get_a(unsigned int pixel);

#ifdef __cplusplus
}
#endif

#endif // JSFX_GFX_API_H
