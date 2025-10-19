# patch.cmake - Apply patches to JSFX source files
# This file contains all source code patches needed to build JSFX as a standalone library

# Patch effectproc.cpp to remove hardcoded "Effects" directory requirement
# This allows loading JSFX files directly from their source location
function(patch_effectproc jsfx_SOURCE_DIR)
    file(READ "${jsfx_SOURCE_DIR}/jsfx/effectproc.cpp" EFFECTPROC_CONTENT)
    string(FIND "${EFFECTPROC_CONTENT}" "sx->m_effectdir.Append" EFFECTS_DIR_FOUND)
    if(NOT EFFECTS_DIR_FOUND EQUAL -1)
        message(STATUS "Patching effectproc.cpp to remove hardcoded Effects directory")
        # Patch Windows path
        string(REPLACE 
            "  sx->m_datadir.Append(\"\\\\Data\");\n  sx->m_effectdir.Append(\"\\\\Effects\");"
            "  // Patched: Don't append hardcoded subdirectories\n  // This allows loading JSFX files from any location"
            EFFECTPROC_CONTENT
            "${EFFECTPROC_CONTENT}")
        # Patch Unix path
        string(REPLACE 
            "  sx->m_datadir.Append(\"/Data\");\n  sx->m_effectdir.Append(\"/Effects\");"
            "  // Patched: Don't append hardcoded subdirectories\n  // This allows loading JSFX files from any location"
            EFFECTPROC_CONTENT
            "${EFFECTPROC_CONTENT}")
        file(WRITE "${jsfx_SOURCE_DIR}/jsfx/effectproc.cpp" "${EFFECTPROC_CONTENT}")
        message(STATUS "effectproc.cpp patched successfully")
    else()
        message(STATUS "effectproc.cpp already patched or pattern not found")
    endif()
endfunction()

# Patch curses.h and related files to avoid macro conflicts with C++ STL
# Rename problematic macros to prevent C++ conflicts
function(patch_curses jsfx_SOURCE_DIR)
    file(READ "${jsfx_SOURCE_DIR}/WDL/win32_curses/curses.h" CURSES_CONTENT)

    # Only patch if not already patched (check for curses_move macro)
    string(FIND "${CURSES_CONTENT}" "curses_move" PATCH_FOUND)
    if(PATCH_FOUND EQUAL -1)
        # Patch move macro conflict with std::move
        string(REPLACE 
            "#define move(y,x) __move(CURSES_INSTANCE,y,x,0)"
            "// #define move(y,x) __move(CURSES_INSTANCE,y,x,0)  /* Disabled - conflicts with std::move */\n#define curses_move(y,x) __move(CURSES_INSTANCE,y,x,0)"
            CURSES_CONTENT 
            "${CURSES_CONTENT}")

        # Patch erase macro conflict with std::string::erase and std::vector::erase
        string(REPLACE 
            "#define erase() curses_erase(CURSES_INSTANCE)"
            "// #define erase() curses_erase(CURSES_INSTANCE)  /* Disabled - conflicts with STL erase */\n#define curses_erase_screen() __curses_erase(CURSES_INSTANCE)"
            CURSES_CONTENT 
            "${CURSES_CONTENT}")

        # Patch sync macro conflict with std::ios::sync
        string(REPLACE 
            "#define sync()"
            "// #define sync()  /* Disabled - conflicts with STL sync */\n#define curses_sync()"
            CURSES_CONTENT 
            "${CURSES_CONTENT}")

        # Patch refresh macro conflict with JUCE and other GUI frameworks
        string(REPLACE 
            "#define refresh()"
            "// #define refresh()  /* Disabled - conflicts with GUI refresh() methods */\n#define curses_refresh()"
            CURSES_CONTENT 
            "${CURSES_CONTENT}")

        file(WRITE "${jsfx_SOURCE_DIR}/WDL/win32_curses/curses.h" "${CURSES_CONTENT}")
        message(STATUS "Applied curses.h macro patches")
    else()
        message(STATUS "curses.h already patched, skipping")
    endif()

    # Patch curses source files to use renamed macros
    foreach(CURSES_FILE 
        "${jsfx_SOURCE_DIR}/WDL/win32_curses/curses_editor.cpp"
        "${jsfx_SOURCE_DIR}/WDL/win32_curses/eel_edit.cpp")
        if(EXISTS "${CURSES_FILE}")
            file(READ "${CURSES_FILE}" FILE_CONTENT)
            # Only patch if not already patched (check for curses_move in file)
            string(FIND "${FILE_CONTENT}" "curses_move" FILE_PATCH_FOUND)
            if(FILE_PATCH_FOUND EQUAL -1)
                # Replace move() calls with curses_move()
                string(REGEX REPLACE "([^a-zA-Z_])move\\(" "\\1curses_move(" FILE_CONTENT "${FILE_CONTENT}")
                # Replace erase() calls with curses_erase_screen() if any exist
                string(REGEX REPLACE "([^a-zA-Z_])erase\\(" "\\1curses_erase_screen(" FILE_CONTENT "${FILE_CONTENT}")
                # Replace sync() calls with curses_sync() if any exist  
                string(REGEX REPLACE "([^a-zA-Z_])sync\\(" "\\1curses_sync(" FILE_CONTENT "${FILE_CONTENT}")
                # Replace refresh() calls with curses_refresh() if any exist
                string(REGEX REPLACE "([^a-zA-Z_])refresh\\(" "\\1curses_refresh(" FILE_CONTENT "${FILE_CONTENT}")
                file(WRITE "${CURSES_FILE}" "${FILE_CONTENT}")
                message(STATUS "Applied patches to ${CURSES_FILE}")
            else()
                message(STATUS "Already patched: ${CURSES_FILE}")
            endif()
        endif()
    endforeach()
endfunction()

# Patch gzguts.h to fix missing unistd.h include on macOS/Unix systems
# This fixes compilation errors with undeclared functions: lseek, read, write, close
function(patch_gzguts jsfx_SOURCE_DIR)
    set(GZGUTS_FILE "${jsfx_SOURCE_DIR}/WDL/zlib/gzguts.h")
    if(EXISTS "${GZGUTS_FILE}")
        file(READ "${GZGUTS_FILE}" GZGUTS_CONTENT)
        # Only patch if not already patched (check for unistd.h include after fcntl.h)
        string(FIND "${GZGUTS_CONTENT}" "#if !defined(_WIN32)\n#include <unistd.h>\n#endif" GZGUTS_PATCH_FOUND)
        if(GZGUTS_PATCH_FOUND EQUAL -1)
            message(STATUS "Patching gzguts.h to include unistd.h on non-Windows platforms")
            # Add unistd.h include for POSIX systems after fcntl.h
            string(REPLACE 
                "#ifndef _POSIX_SOURCE\n#define _POSIX_SOURCE\n#endif\n#include <fcntl.h>"
                "#ifndef _POSIX_SOURCE\n#define _POSIX_SOURCE\n#endif\n#include <fcntl.h>\n\n#if !defined(_WIN32)\n#include <unistd.h>\n#endif"
                GZGUTS_CONTENT "${GZGUTS_CONTENT}")
            file(WRITE "${GZGUTS_FILE}" "${GZGUTS_CONTENT}")
            message(STATUS "gzguts.h patched successfully")
        else()
            message(STATUS "gzguts.h already patched, skipping")
        endif()
        
        # Ensure unistd.h is also included more broadly for all POSIX systems
        string(FIND "${GZGUTS_CONTENT}" "#include <stdio.h>" STDIO_FOUND)
        string(FIND "${GZGUTS_CONTENT}" "#if defined(__unix__) || defined(__APPLE__)\n#include <unistd.h>\n#endif" BROAD_UNISTD_FOUND)
        if(STDIO_FOUND GREATER -1 AND BROAD_UNISTD_FOUND EQUAL -1)
            message(STATUS "Adding broader unistd.h include for POSIX systems")
            string(REPLACE 
                "#include <stdio.h>"
                "#include <stdio.h>\n\n#if defined(__unix__) || defined(__APPLE__)\n#include <unistd.h>\n#endif"
                GZGUTS_CONTENT "${GZGUTS_CONTENT}")
            file(WRITE "${GZGUTS_FILE}" "${GZGUTS_CONTENT}")
        endif()
    endif()
endfunction()

# Patch pngpriv.h to fix fp.h include issue on modern macOS
# This prevents the "fp.h file not found" error by using math.h instead
function(patch_pngpriv jsfx_SOURCE_DIR)
    set(PNGPRIV_FILE "${jsfx_SOURCE_DIR}/WDL/libpng/pngpriv.h")
    if(EXISTS "${PNGPRIV_FILE}")
        file(READ "${PNGPRIV_FILE}" PNGPRIV_CONTENT)
        # Only patch if not already patched (check for !defined(__APPLE__) condition)
        string(FIND "${PNGPRIV_CONTENT}" "(defined(TARGET_OS_MAC) && !defined(__APPLE__))" PNGPRIV_PATCH_FOUND)
        if(PNGPRIV_PATCH_FOUND EQUAL -1)
            message(STATUS "Patching pngpriv.h to exclude modern macOS from using fp.h")
            # Modify the condition to exclude modern macOS (which defines __APPLE__)
            string(REPLACE 
                "|| defined(TARGET_OS_MAC)"
                "|| (defined(TARGET_OS_MAC) && !defined(__APPLE__))"
                PNGPRIV_CONTENT "${PNGPRIV_CONTENT}")
            file(WRITE "${PNGPRIV_FILE}" "${PNGPRIV_CONTENT}")
            message(STATUS "pngpriv.h patched successfully")
        else()
            message(STATUS "pngpriv.h already patched, skipping")
        endif()
    endif()
endfunction()

# Patch pngpriv.h header inclusion order - pngstruct.h must come before pnginfo.h
# This fixes "unknown type name 'png_colorspace'" error
function(patch_pngpriv_header_order jsfx_SOURCE_DIR)
    set(PNGPRIV_HEADER_ORDER_FILE "${jsfx_SOURCE_DIR}/WDL/libpng/pngpriv.h")
    if(EXISTS "${PNGPRIV_HEADER_ORDER_FILE}")
        file(READ "${PNGPRIV_HEADER_ORDER_FILE}" PNGPRIV_ORDER_CONTENT)
        # Only patch if not already fixed (check for pngstruct.h before pnginfo.h)
        string(FIND "${PNGPRIV_ORDER_CONTENT}" "#include \"pnginfo.h\"\n#include \"pngstruct.h\"" HEADER_ORDER_ISSUE)
        if(NOT HEADER_ORDER_ISSUE EQUAL -1)
            message(STATUS "Fixing pngpriv.h header inclusion order (pngstruct.h before pnginfo.h)")
            # Fix the header order - swap the two includes
            string(REPLACE 
                "#include \"pnginfo.h\"\n#include \"pngstruct.h\""
                "#include \"pngstruct.h\"\n#include \"pnginfo.h\""
                PNGPRIV_ORDER_CONTENT "${PNGPRIV_ORDER_CONTENT}")
            file(WRITE "${PNGPRIV_HEADER_ORDER_FILE}" "${PNGPRIV_ORDER_CONTENT}")
            message(STATUS "pngpriv.h header order fixed successfully")
        else()
            message(STATUS "pngpriv.h header order already correct, skipping")
        endif()
    endif()
endfunction()

# Patch jsfx_api.cpp to disable DllMain for standalone builds
# Apply on all platforms to ensure consistent API
function(patch_jsfx_api jsfx_SOURCE_DIR)
    set(JSFX_API_FILE "${jsfx_SOURCE_DIR}/jsfx/jsfx_api.cpp")
    if(EXISTS "${JSFX_API_FILE}")
        file(READ "${JSFX_API_FILE}" API_CONTENT)
        # Only patch if not already patched (check for #if 0 before DllMain)
        string(FIND "${API_CONTENT}" "#if 0  // Disabled for standalone builds" API_PATCH_FOUND)
        if(API_PATCH_FOUND EQUAL -1)
            # Remove extern "C" { wrapper
            string(REPLACE 
                "extern \"C\" {\n\n#ifdef _WIN32"
                "#ifdef _WIN32"
                API_CONTENT "${API_CONTENT}")
            
            # Simplify JesusonicAPI declaration - remove platform-specific export declarations
            string(REPLACE 
                "#ifdef _WIN32\n__declspec(dllexport) jsfxAPI JesusonicAPI=\n#else\n__attribute__((visibility(\"default\"))) jsfxAPI JesusonicAPI=\n#endif"
                "jsfxAPI JesusonicAPI="
                API_CONTENT "${API_CONTENT}")
            
            # Comment out the DllMain function for standalone builds (on all platforms)
            # Wrap it in #if 0 ... #endif (do this before removing };)
            string(REPLACE 
                "BOOL WINAPI DllMain"
                "#if 0  // Disabled for standalone builds\nBOOL WINAPI DllMain"
                API_CONTENT "${API_CONTENT}")
            # Find the closing brace and return statement, add #endif after
            string(REPLACE 
                "}\n\n};\n\nextern int g_last_srate;"
                "}\n#endif  // Disabled for standalone builds\n\n\nextern int g_last_srate;"
                API_CONTENT "${API_CONTENT}")
            
            file(WRITE "${JSFX_API_FILE}" "${API_CONTENT}")
            message(STATUS "Applied DllMain and extern C patches to jsfx_api.cpp")
        else()
            message(STATUS "jsfx_api.cpp already patched, skipping")
        endif()
    endif()
endfunction()

# Patch localize.cpp to use std::min instead of min macro (when NOMINMAX is defined)
# AND force ANSI dialog creation to match ANSI resources
# AND disable Windows-specific code on non-Windows platforms
function(patch_localize jsfx_SOURCE_DIR)
    set(LOCALIZE_FILE "${jsfx_SOURCE_DIR}/WDL/localize/localize.cpp")
    if(EXISTS "${LOCALIZE_FILE}")
        file(READ "${LOCALIZE_FILE}" LOCALIZE_CONTENT)
        # Only patch if not already patched
        string(FIND "${LOCALIZE_CONTENT}" "#include <algorithm>" LOCALIZE_PATCH_FOUND)
        if(LOCALIZE_PATCH_FOUND EQUAL -1)
            # Add algorithm header AFTER win32_utf8.h to avoid include order issues
            string(REPLACE 
                "#include \"../win32_utf8.h\""
                "#include \"../win32_utf8.h\"\n#include <algorithm>"
                LOCALIZE_CONTENT "${LOCALIZE_CONTENT}")
            
            # Replace min() with (std::min)() to handle NOMINMAX
            string(REGEX REPLACE "([^:a-zA-Z_])min\\(" "\\1(std::min)(" LOCALIZE_CONTENT "${LOCALIZE_CONTENT}")
            
            # Replace max() with (std::max)() to handle NOMINMAX
            string(REGEX REPLACE "([^:a-zA-Z_])max\\(" "\\1(std::max)(" LOCALIZE_CONTENT "${LOCALIZE_CONTENT}")
            
            # Wrap Windows-specific static control styles in #ifdef _WIN32
            string(REPLACE 
                "    if (!(GetWindowLong(hwnd,GWL_STYLE)&(SS_RIGHT|SS_CENTER))) t.mode = windowReorgEnt::WRET_SIZEADJ;"
                "#ifdef _WIN32\n    if (!(GetWindowLong(hwnd,GWL_STYLE)&(SS_RIGHT|SS_CENTER))) t.mode = windowReorgEnt::WRET_SIZEADJ;\n#endif"
                LOCALIZE_CONTENT "${LOCALIZE_CONTENT}")
            
            # Wrap Windows-specific dialog creation in #ifdef _WIN32
            # Find the switch statement and wrap the entire switch in #ifdef
            string(REPLACE 
                "  switch (mode)\n  {\n    case 0: return CreateDialogParam(hInstance,lpTemplate,hwndParent,dlgProc,lParam);\n    case 1: return (HWND) (INT_PTR)DialogBoxParam(hInstance,lpTemplate,hwndParent,dlgProc,lParam);\n  }"
                "#ifdef _WIN32\n  switch (mode)\n  {\n    case 0: return CreateDialogParamA(hInstance,lpTemplate,hwndParent,dlgProc,lParam);\n    case 1: return (HWND) (INT_PTR)DialogBoxParamA(hInstance,lpTemplate,hwndParent,dlgProc,lParam);\n  }\n#else\n  switch (mode)\n  {\n    case 0: return CreateDialogParam(hInstance,lpTemplate,hwndParent,dlgProc,lParam);\n    case 1: return (HWND) (INT_PTR)DialogBoxParam(hInstance,lpTemplate,hwndParent,dlgProc,lParam);\n  }\n#endif"
                LOCALIZE_CONTENT "${LOCALIZE_CONTENT}")
            
            file(WRITE "${LOCALIZE_FILE}" "${LOCALIZE_CONTENT}")
            message(STATUS "Applied min/max, ANSI dialog, and platform-specific patches to localize.cpp")
        else()
            message(STATUS "localize.cpp already patched, skipping")
        endif()
    endif()
endfunction()

# Patch slider-control.cpp to fix VertSliderProc undefined reference
# The function is forward declared but never implemented - change to HorzSliderProc
function(patch_slider_control_vertslider jsfx_SOURCE_DIR)
    set(SLIDER_CONTROL_FILE "${jsfx_SOURCE_DIR}/jsfx/standalone-helpers/slider-control.cpp")
    if(EXISTS "${SLIDER_CONTROL_FILE}")
        file(READ "${SLIDER_CONTROL_FILE}" SLIDER_CONTENT)
        # Check if already patched
        string(FIND "${SLIDER_CONTENT}" "hw=CreateDialog(NULL,0,parent,(DLGPROC)HorzSliderProc);" SLIDER_PATCH_FOUND)
        if(SLIDER_PATCH_FOUND EQUAL -1)
            # Replace VertSliderProc with HorzSliderProc in the CreateDialog call
            string(REPLACE 
                "hw=CreateDialog(NULL,0,parent,(DLGPROC)VertSliderProc);"
                "hw=CreateDialog(NULL,0,parent,(DLGPROC)HorzSliderProc);"
                SLIDER_CONTENT "${SLIDER_CONTENT}")
            
            # Remove the forward declaration of VertSliderProc
            string(REPLACE 
                "static LRESULT WINAPI VertSliderProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);"
                "// Forward declaration removed - using HorzSliderProc instead"
                SLIDER_CONTENT "${SLIDER_CONTENT}")
            
            file(WRITE "${SLIDER_CONTROL_FILE}" "${SLIDER_CONTENT}")
            message(STATUS "Applied VertSliderProc fix to slider-control.cpp")
        else()
            message(STATUS "slider-control.cpp already patched, skipping")
        endif()
    endif()
endfunction()

# Patch lice_loader_init.cpp to fix _stricmp usage on non-Windows platforms
# _stricmp is Windows-specific, replace with strcasecmp for Unix/macOS
function(patch_lice_loader CMAKE_CURRENT_SOURCE_DIR)
    set(LICE_LOADER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/lice_loader_init.cpp")
    if(EXISTS "${LICE_LOADER_FILE}")
        file(READ "${LICE_LOADER_FILE}" LICE_LOADER_CONTENT)
        # Only patch if not already patched (check for strcasecmp)
        string(FIND "${LICE_LOADER_CONTENT}" "strcasecmp" LICE_LOADER_PATCH_FOUND)
        if(LICE_LOADER_PATCH_FOUND EQUAL -1)
            message(STATUS "Patching lice_loader_init.cpp to use strcasecmp instead of _stricmp")
            # Replace _stricmp with strcasecmp for Unix/macOS compatibility
            string(REPLACE 
                "_stricmp(ext, \".png\")"
                "strcasecmp(ext, \".png\")"
                LICE_LOADER_CONTENT "${LICE_LOADER_CONTENT}")
            file(WRITE "${LICE_LOADER_FILE}" "${LICE_LOADER_CONTENT}")
            message(STATUS "lice_loader_init.cpp patched successfully")
        else()
            message(STATUS "lice_loader_init.cpp already patched, skipping")
        endif()
    endif()
endfunction()

# Patch slider-control.cpp on all platforms to include <algorithm> and fix DoSize call signature
function(patch_slider_control jsfx_SOURCE_DIR)
    set(SLIDER_FILE "${jsfx_SOURCE_DIR}/jsfx/standalone-helpers/slider-control.cpp")
    if(EXISTS "${SLIDER_FILE}")
        file(READ "${SLIDER_FILE}" SLIDER_CONTENT)
        string(FIND "${SLIDER_CONTENT}" "#include <algorithm>" SLIDER_ALGO_FOUND)
        if(SLIDER_ALGO_FOUND EQUAL -1)
            string(REPLACE "#include <math.h>" "#include <math.h>\n#include <algorithm>\nusing std::max;\nusing std::min;" SLIDER_CONTENT "${SLIDER_CONTENT}")
            file(WRITE "${SLIDER_FILE}" "${SLIDER_CONTENT}")
            message(STATUS "Patched slider-control.cpp to include <algorithm> and bring std::max/min into scope")
        endif()
        
        string(FIND "${SLIDER_CONTENT}" "int newW = " DOSIZE_PATCH_FOUND)
        if(DOSIZE_PATCH_FOUND EQUAL -1)
            string(REPLACE 
                "m_bm1.DoSize(ps.hdc,max(m_bm1.GetW(),r.right),max(m_bm1.GetH(),r.bottom));"
                "int newW = (std::max)(m_bm1.GetW(), (int)r.right);\n            int newH = (std::max)(m_bm1.GetH(), (int)r.bottom);\n            m_bm1.DoSize(ps.hdc, newW, newH);"
                SLIDER_CONTENT "${SLIDER_CONTENT}")
            file(WRITE "${SLIDER_FILE}" "${SLIDER_CONTENT}")
            message(STATUS "Patched DoSize call to use explicit std::max calls with type casting")
        else()
            message(STATUS "DoSize call already patched, skipping")
        endif()
        
        string(FIND "${SLIDER_CONTENT}" "/* JUCE_BITMAP_PATCH */" BITMAP_PATCH_FOUND)
        if(BITMAP_PATCH_FOUND EQUAL -1)
            string(REPLACE 
                "HBITMAP STYLE_GetSliderBitmap(bool isVert, int *w, int *h)\n{\n  if (isVert)"
                "/* JUCE_BITMAP_PATCH */\nvoid Sliders_SetBitmap(HBITMAP hBitmap, bool isVert)\n{\n  if (isVert)\n    standalone_icontheme.fader_bitmap_v = hBitmap;\n  else\n    standalone_icontheme.fader_bitmap_h = hBitmap;\n}\n\nHBITMAP STYLE_GetSliderBitmap(bool isVert, int *w, int *h)\n{\n  if (isVert)"
                SLIDER_CONTENT "${SLIDER_CONTENT}")
            file(WRITE "${SLIDER_FILE}" "${SLIDER_CONTENT}")
            message(STATUS "Added Sliders_SetBitmap function to allow external bitmap initialization")
        else()
            message(STATUS "Bitmap setting function already present, skipping")
        endif()
        
        # Fix negative shift warning
        string(FIND "${SLIDER_CONTENT}" "SHIFT_NEGATIVE_FIX" SHIFT_PATCH_FOUND)
        if(SHIFT_PATCH_FOUND EQUAL -1)
            string(REPLACE 
                "        SendMessage(hwnd,WM_MOUSEWHEEL,wParam == VK_UP ? (120<<16) : (-120<<16),0);"
                "        /* SHIFT_NEGATIVE_FIX */ SendMessage(hwnd,WM_MOUSEWHEEL,wParam == VK_UP ? (120<<16) : (static_cast<WPARAM>(-120)<<16),0);"
                SLIDER_CONTENT "${SLIDER_CONTENT}")
            file(WRITE "${SLIDER_FILE}" "${SLIDER_CONTENT}")
            message(STATUS "Fixed negative shift warning in slider-control.cpp")
        endif()
        
        # Fix VertSliderProc - it's actually defined later in the file, need to forward declare
        string(FIND "${SLIDER_CONTENT}" "LRESULT WINAPI VertSliderProc" VERTSLIDER_DECL_FOUND)
        if(VERTSLIDER_DECL_FOUND EQUAL -1)
            # The function is defined later in the file, we need a forward declaration
            string(REPLACE 
                "#include <math.h>\n#include <algorithm>"
                "#include <math.h>\n#include <algorithm>\n\n// Forward declaration for non-Windows platforms\n#ifndef _WIN32\nstatic LRESULT WINAPI VertSliderProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);\n#endif"
                SLIDER_CONTENT "${SLIDER_CONTENT}")
            file(WRITE "${SLIDER_FILE}" "${SLIDER_CONTENT}")
            message(STATUS "Added forward declaration for VertSliderProc")
        endif()
        
        # Fix VST_Standalone_Init - it's not defined in JSFX, comment out the code that uses it
        string(FIND "${SLIDER_CONTENT}" "/* VST_Standalone_Init removed" VST_INIT_PATCH_FOUND)
        if(VST_INIT_PATCH_FOUND EQUAL -1)
            string(REPLACE 
                "    Dl_info inf={0,};\n    dladdr((void *)VST_Standalone_Init,&inf);"
                "    Dl_info inf={0,};\n    /* VST_Standalone_Init removed - not available in JSFX standalone */\n    // dladdr((void *)VST_Standalone_Init,&inf);"
                SLIDER_CONTENT "${SLIDER_CONTENT}")
            file(WRITE "${SLIDER_FILE}" "${SLIDER_CONTENT}")
            message(STATUS "Commented out VST_Standalone_Init reference")
        endif()
        
        # Fix class name mismatch: dialog resource uses "REAPERknob" but slider creator checks for "jsfx_slider"
        string(FIND "${SLIDER_CONTENT}" "REAPERknob" REAPER_KNOB_PATCH_FOUND)
        if(REAPER_KNOB_PATCH_FOUND EQUAL -1)
            string(REPLACE 
                "  if (!strcmp(classname,\"jsfx_slider\"))"
                "  if (!strcmp(classname,\"jsfx_slider\") || !strcmp(classname,\"REAPERknob\"))"
                SLIDER_CONTENT "${SLIDER_CONTENT}")
            file(WRITE "${SLIDER_FILE}" "${SLIDER_CONTENT}")
            message(STATUS "Patched slider-control.cpp to recognize REAPERknob class name")
        endif()
    endif()
endfunction()

# Patch meter-control.cpp on all platforms to include <algorithm> and SWELL headers
function(patch_meter_control jsfx_SOURCE_DIR)
    set(METER_FILE "${jsfx_SOURCE_DIR}/jsfx/standalone-helpers/meter-control.cpp")
    if(EXISTS "${METER_FILE}")
        file(READ "${METER_FILE}" METER_CONTENT)
        string(FIND "${METER_CONTENT}" "#include <algorithm>" METER_ALGO_FOUND)
        if(METER_ALGO_FOUND EQUAL -1)
            # Add SWELL headers for non-Windows platforms and algorithm header
            string(REPLACE 
                "#ifdef _WIN32\n#include <windowsx.h>\n#include <windows.h>\n#endif\n\n#include <math.h>"
                "#ifdef _WIN32\n#include <windowsx.h>\n#include <windows.h>\n#else\n#include \"../../WDL/swell/swell.h\"\n#include \"../../WDL/swell/swell-types.h\"\n#endif\n\n#include <math.h>\n#include <algorithm>\nusing std::max;\nusing std::min;"
                METER_CONTENT "${METER_CONTENT}")
            
            # Fix class name mismatch: dialog resource uses "REAPERvertvu" but meter creator checks for "jsfx_meter"
            string(REPLACE 
                "if (!strcmp(classname,\"jsfx_meter\"))"
                "if (!strcmp(classname,\"jsfx_meter\") || !strcmp(classname,\"REAPERvertvu\"))"
                METER_CONTENT "${METER_CONTENT}")
            
            file(WRITE "${METER_FILE}" "${METER_CONTENT}")
            message(STATUS "Patched meter-control.cpp to include SWELL headers, <algorithm>, and fix REAPERvertvu class name")
        else()
            message(STATUS "meter-control.cpp already patched, skipping")
        endif()
    endif()
endfunction()

# Main function to apply all patches
function(apply_jsfx_patches jsfx_SOURCE_DIR CMAKE_CURRENT_SOURCE_DIR)
    message(STATUS "Applying JSFX source patches...")
    
    patch_effectproc("${jsfx_SOURCE_DIR}")
    patch_curses("${jsfx_SOURCE_DIR}")
    patch_gzguts("${jsfx_SOURCE_DIR}")
    patch_pngpriv("${jsfx_SOURCE_DIR}")
    patch_pngpriv_header_order("${jsfx_SOURCE_DIR}")
    patch_jsfx_api("${jsfx_SOURCE_DIR}")
    patch_localize("${jsfx_SOURCE_DIR}")
    patch_slider_control_vertslider("${jsfx_SOURCE_DIR}")
    patch_lice_loader("${CMAKE_CURRENT_SOURCE_DIR}")
    patch_slider_control("${jsfx_SOURCE_DIR}")
    patch_meter_control("${jsfx_SOURCE_DIR}")
    
    message(STATUS "All JSFX patches applied successfully")
endfunction()
