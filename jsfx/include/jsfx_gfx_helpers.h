/*
 * JSFX GFX Access Helpers - Public API
 *
 * These functions provide safe access to JSFX gfx variables without
 * requiring external code to include eel_lice.h
 */

#ifndef JSFX_GFX_HELPERS_H
#define JSFX_GFX_HELPERS_H

#ifdef __cplusplus
extern "C"
{
#endif

    struct SX_Instance;

    // Get requested gfx dimensions from @gfx declaration
    void jsfx_get_gfx_requested_size(SX_Instance* instance, int* w, int* h);

    // Set mouse position
    void jsfx_set_mouse_pos(SX_Instance* instance, double x, double y);

    // Set mouse capture state (bitfield: 1=left, 2=right, 4=ctrl, 8=shift, 16=middle)
    void jsfx_set_mouse_cap(SX_Instance* instance, int cap);

    // Set mouse wheel delta
    void jsfx_set_mouse_wheel(SX_Instance* instance, double delta);

    // Set horizontal mouse wheel delta
    void jsfx_set_mouse_hwheel(SX_Instance* instance, double delta);

    // Set gfx dimensions (this updates gfx.w and gfx.h variables)
    void jsfx_set_gfx_size(SX_Instance* instance, int w, int h);

#ifdef __cplusplus
}
#endif

#endif // JSFX_GFX_HELPERS_H
