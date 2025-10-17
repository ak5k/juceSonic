cmake_minimum_required(VERSION 3.28)
project(jsfx VERSION 0.1.0 LANGUAGES C CXX)

# On macOS, build universal binaries (arm64 + x86_64)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS" FORCE)
    message(STATUS "Building universal binary for macOS (arm64 + x86_64)")
endif()

# Use JSFX from git submodule instead of FetchContent
set(jsfx_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/jsfx")
message(STATUS "JSFX source directory: ${jsfx_SOURCE_DIR}")

# Check if submodule is initialized
if(NOT EXISTS "${jsfx_SOURCE_DIR}/jsfx/effectproc.cpp")
    message(FATAL_ERROR "JSFX submodule not initialized. Run: git submodule update --init --recursive")
endif()

# Patch effectproc.cpp to remove hardcoded "Effects" directory requirement
# This allows loading JSFX files directly from their source location
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

# Patch curses.h and related files to avoid macro conflicts with C++ STL
# Rename problematic macros to prevent C++ conflicts
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

# Patch jsfx_api.cpp to disable DllMain for standalone builds
# Apply on all platforms to ensure consistent API
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

# Generate SWELL resource files from res.rc (needed for jsfx_api.cpp)
if(NOT WIN32)
    find_program(PERL_EXECUTABLE perl)
    if(PERL_EXECUTABLE)
        # Generate SWELL resources at configure time
        if(NOT EXISTS "${jsfx_SOURCE_DIR}/jsfx/res.rc_mac_dlg")
            message(STATUS "Generating SWELL resource files...")
            execute_process(
                COMMAND ${PERL_EXECUTABLE} ${jsfx_SOURCE_DIR}/WDL/swell/swell_resgen.pl
                        --quiet ${jsfx_SOURCE_DIR}/jsfx/res.rc
                WORKING_DIRECTORY ${jsfx_SOURCE_DIR}/jsfx
                RESULT_VARIABLE SWELL_RESULT
            )
            if(NOT SWELL_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to generate SWELL resource files")
            endif()
        endif()
        set(SWELL_RESOURCES ${jsfx_SOURCE_DIR}/jsfx/res.rc_mac_dlg)
    else()
        message(FATAL_ERROR "Perl not found - required for SWELL resource generation. Please install perl.")
    endif()
endif()

# Patch localize.cpp to use std::min instead of min macro (when NOMINMAX is defined)
# AND force ANSI dialog creation to match ANSI resources
# AND disable Windows-specific code on non-Windows platforms
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

# Patch slider-control.cpp to fix VertSliderProc undefined reference
# The function is forward declared but never implemented - change to HorzSliderProc
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


# Create JSFX as a static library (not INTERFACE) to encapsulate all internal details
add_library(${PROJECT_NAME} STATIC)

# Add LICE sources (required for all platforms - core graphics library)
file(GLOB LICE_SOURCES 
    "${jsfx_SOURCE_DIR}/WDL/lice/lice.cpp"
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_arc.cpp"
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_colorspace.cpp"
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_line.cpp"
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_text.cpp"
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_textnew.cpp"
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_ico.cpp"
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_bmp.cpp"
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_image.cpp"
)

# Add core JSFX sources (common to all platforms)
target_sources(jsfx PRIVATE 
    # Main JSFX sources
    ${jsfx_SOURCE_DIR}/jsfx/effectload.cpp
    ${jsfx_SOURCE_DIR}/jsfx/effectproc.cpp
    ${jsfx_SOURCE_DIR}/jsfx/jsfx_api.cpp
    ${jsfx_SOURCE_DIR}/jsfx/miscfunc.cpp
    ${jsfx_SOURCE_DIR}/jsfx/sfxui.cpp
    ${jsfx_SOURCE_DIR}/jsfx/sfx_edit.cpp

    # stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/jsfx_stubs.cpp
    
    # WDL GUI sources
    ${jsfx_SOURCE_DIR}/WDL/wingui/wndsize.cpp
    
    # WDL Curses sources
    ${jsfx_SOURCE_DIR}/WDL/win32_curses/curses_win32.cpp
    ${jsfx_SOURCE_DIR}/WDL/win32_curses/curses_editor.cpp
    ${jsfx_SOURCE_DIR}/WDL/win32_curses/eel_edit.cpp
    
    # EEL2 (Expression Evaluation Language) sources
    ${jsfx_SOURCE_DIR}/WDL/eel2/nseel-lextab.c
    ${jsfx_SOURCE_DIR}/WDL/eel2/nseel-cfunc.c
    ${jsfx_SOURCE_DIR}/WDL/eel2/nseel-caltab.c
    ${jsfx_SOURCE_DIR}/WDL/eel2/nseel-compiler.c
    ${jsfx_SOURCE_DIR}/WDL/eel2/nseel-eval.c
    ${jsfx_SOURCE_DIR}/WDL/eel2/nseel-ram.c
    ${jsfx_SOURCE_DIR}/WDL/eel2/nseel-yylex.c
    
    # WDL FFT sources
    ${jsfx_SOURCE_DIR}/WDL/fft.c
    
    # # WDL UTF8 support (Windows) - included directly in effectload.cpp, do not compile separately
    # ${jsfx_SOURCE_DIR}/WDL/win32_utf8.c
    
    # LICE graphics library (core for all platforms)
    ${LICE_SOURCES}
    
    # WDL localization (required for all platforms)
    ${jsfx_SOURCE_DIR}/WDL/localize/localize.cpp
    
    # JSFX standalone helpers (required for all platforms - work through SWELL on non-Windows)
    ${jsfx_SOURCE_DIR}/jsfx/standalone-helpers/slider-control.cpp
    ${jsfx_SOURCE_DIR}/jsfx/standalone-helpers/meter-control.cpp
)

# Avoid duplicate symbol for g_config_slider_classname which is also defined in jsfx_api.cpp
# Rename the symbol in slider-control.cpp to a TU-local name (on all platforms)
set_source_files_properties(${jsfx_SOURCE_DIR}/jsfx/standalone-helpers/slider-control.cpp
    PROPERTIES COMPILE_DEFINITIONS "g_config_slider_classname=jsfx_slider_config_classname_local")

# Platform-specific configuration
if(WIN32)
    # Windows - use native Win32 API (no SWELL needed)
    target_sources(jsfx PRIVATE
        ${jsfx_SOURCE_DIR}/jsfx/res.rc
    )
    target_compile_definitions(jsfx PUBLIC _WIN32=1)
else()
    # Non-Windows platforms - use SWELL (Simple Windows Emulation Layer)
    target_sources(jsfx PRIVATE
        ${SWELL_RESOURCES}  # Generated SWELL resource file
        
        # SWELL common sources
        ${jsfx_SOURCE_DIR}/WDL/swell/swell.cpp
        ${jsfx_SOURCE_DIR}/WDL/swell/swell-ini.cpp
    )
    
    if(APPLE)
        # macOS - use native Cocoa backend with .mm files
        # Note: Cocoa handles the message loop, no need for SWELL_RunMessageLoop
        target_sources(jsfx PRIVATE
            ${jsfx_SOURCE_DIR}/WDL/swell/swell-dlg.mm
            ${jsfx_SOURCE_DIR}/WDL/swell/swell-gdi.mm
            ${jsfx_SOURCE_DIR}/WDL/swell/swell-wnd.mm
            ${jsfx_SOURCE_DIR}/WDL/swell/swell-menu.mm
            ${jsfx_SOURCE_DIR}/WDL/swell/swell-misc.mm
            ${jsfx_SOURCE_DIR}/WDL/swell/swell-miscdlg.mm
            ${jsfx_SOURCE_DIR}/WDL/swell/swell-kb.mm
            ${jsfx_SOURCE_DIR}/WDL/swell/swell-modstub.mm
            ${jsfx_SOURCE_DIR}/WDL/swell/swell-appstub.mm
        )
        target_link_libraries(jsfx PUBLIC "-framework Cocoa" "-framework Carbon")
    else()
        # Linux/Unix - use GDK backend with .cpp files
        file(GLOB SWELL_GDK_SOURCES "${jsfx_SOURCE_DIR}/WDL/swell/swell*.cpp")
        target_sources(jsfx PRIVATE ${SWELL_GDK_SOURCES})
        
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
        find_package(X11 REQUIRED)
        
        if(NOT X11_Xi_FOUND)
            message(FATAL_ERROR "X11 Xi (Input extension) library not found")
        endif()
        
        target_compile_definitions(jsfx PUBLIC 
            SWELL_TARGET_GDK=3
            SWELL_LICE_GDI
        )
        target_include_directories(jsfx PUBLIC ${GTK3_INCLUDE_DIRS})
        target_link_libraries(jsfx PUBLIC ${GTK3_LIBRARIES} ${X11_LIBRARIES} ${X11_Xi_LIB} GL)
        set_target_properties(jsfx PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endif()

# Patch slider-control.cpp on all platforms to include <algorithm> and fix DoSize call signature
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

# Patch meter-control.cpp on all platforms to include <algorithm> and SWELL headers
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

# Add x86_64 assembly optimization if available
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    if(EXISTS "${jsfx_SOURCE_DIR}/WDL/eel2/asm-nseel-x64-sse.asm")
        # Note: This would require NASM to be available
        # For now, we'll skip the assembly optimization
        message(STATUS "x86_64 detected, but skipping assembly optimization (requires NASM)")
    endif()
endif()

# Set compiler flags based on Makefile
target_compile_definitions(jsfx PUBLIC
    WDL_FFT_REALSIZE=8
    FORCE_WIN32_CURSES        # Required for curses emulation
    WDL_NO_DEFINE_MINMAX      # Prevent SWELL from defining min/max macros
    NOMINMAX                  # Windows equivalent
    EEL_TARGET_PORTABLE       # Use portable EEL2 (no assembly)
    $<$<CONFIG:Debug>:_DEBUG;WDL_CHECK_FOR_NON_UTF8_FOPEN>
    $<$<CONFIG:Release>:NDEBUG>
)

# Note: _FILE_OFFSET_BITS=64 is not set as INTERFACE to avoid conflicts with JUCE's internal zlib
# JSFX sources will compile with whatever the consuming project uses

# Set compile flags similar to Makefile
if(MSVC)
    target_compile_options(jsfx PRIVATE
        /wd4100
        /wd4244
        /wd4245
        /wd4505
        /wd4456
        /wd4457
        /wd4458
        /wd4996  # deprecated functions
    )
else()
    target_compile_options(jsfx PRIVATE
        -Wno-error
        -fvisibility=hidden
        -fstrict-aliasing
        -fno-math-errno
        -Wall
        -Wtype-limits
        -Wno-unused-parameter
        -Wno-sign-compare
        -Wno-unused-function
        -Wno-shift-negative-value
        $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wno-format-truncation>
        $<$<PLATFORM_ID:Darwin>:-Wno-deprecated>
        $<$<PLATFORM_ID:Darwin>:-Wno-switch>
        $<$<PLATFORM_ID:Darwin>:-Wno-c++11-narrowing>
    )
endif()

# Create the public interface library that users will link against
add_library(JSFX::jsfx ALIAS jsfx)  # Modern CMake namespaced alias

# Expose public include directories  
# LICE and eel headers are PUBLIC so users can access gfx state
# This is necessary because static library hides internal includes
target_include_directories(jsfx 
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${jsfx_SOURCE_DIR}>  # For WDL/lice and WDL/eel2 access
        $<BUILD_INTERFACE:${jsfx_SOURCE_DIR}/jsfx>  # For jsfx headers like sfxui.h
    PRIVATE
        ${jsfx_SOURCE_DIR}/jmde
)

# These were INTERFACE, but now we only need to expose the public API
# WDL_NO_DEFINE_MINMAX is already in PUBLIC compile definitions above
target_compile_definitions(jsfx PUBLIC
    LOCALIZE_DISABLE=1
    LOCALIZE_NO_DIALOG_MENU_REDEF=1
)
