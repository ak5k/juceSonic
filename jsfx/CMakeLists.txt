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

# Apply all source code patches
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch.cmake)
apply_jsfx_patches("${jsfx_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")

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

# Create JSFX as a static library
add_library(${PROJECT_NAME} STATIC)

# Configure EEL2 Assembly Optimizations (REQUIRED)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/EEL2Assembly.cmake)

# Add WDL image library dependencies (required for LICE image loading)
file(GLOB ZLIB_SOURCES "${jsfx_SOURCE_DIR}/WDL/zlib/*.c")
file(GLOB LIBPNG_SOURCES "${jsfx_SOURCE_DIR}/WDL/libpng/*.c")
file(GLOB LIBJPEG_SOURCES "${jsfx_SOURCE_DIR}/WDL/jpeglib/*.c")
list(REMOVE_ITEM LIBJPEG_SOURCES "${jsfx_SOURCE_DIR}/WDL/jpeglib/example.c")
file(GLOB GIFLIB_SOURCES "${jsfx_SOURCE_DIR}/WDL/giflib/*.c")

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
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_png.cpp"
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_jpg.cpp"
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_gif.cpp"
    "${jsfx_SOURCE_DIR}/WDL/lice/lice_pcx.cpp"
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
    
    # LICE loader initialization (ensures image loaders are registered)
    ${CMAKE_CURRENT_SOURCE_DIR}/lice_loader_init.cpp
    
    # WDL image library dependencies
    ${ZLIB_SOURCES}
    ${LIBPNG_SOURCES}
    ${LIBJPEG_SOURCES}
    ${GIFLIB_SOURCES}
    
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

# Add EEL2 assembly optimizations to the jsfx target
# Note: For ARM64 Linux with GCC inline assembly, asm-nseel-aarch64-gcc.c is #include'd by nseel-cfunc.c
# We must NOT add it as a separate source to avoid multiple definitions
if(NOT EEL2_USE_GCC_ASM)
    eel2_add_assembly_to_target(jsfx)
else()
    # GCC inline assembly is included via nseel-cfunc.c - just add the required defines to that file
    set_source_files_properties(${jsfx_SOURCE_DIR}/WDL/eel2/nseel-cfunc.c
        PROPERTIES 
            COMPILE_DEFINITIONS "NSEEL_RAM_BLOCKS_LOG2=11;NSEEL_RAM_ITEMSPERBLOCK_LOG2=16;NSEEL_RAM_BLOCKS=(1<<11);NSEEL_RAM_ITEMSPERBLOCK=(1<<16);EEL_F_SIZE=8"
    )
    message(STATUS "EEL2: GCC inline assembly will be included via nseel-cfunc.c (with required defines)")
endif()

# Set compiler flags based on Makefile
target_compile_definitions(jsfx PUBLIC
    WDL_FFT_REALSIZE=8
    FORCE_WIN32_CURSES        # Required for curses emulation
    WDL_NO_DEFINE_MINMAX      # Prevent SWELL from defining min/max macros
    NOMINMAX                  # Windows equivalent
    LICE_FAVOR_SPEED
    $<$<CONFIG:Debug>:_DEBUG;WDL_CHECK_FOR_NON_UTF8_FOPEN>
    $<$<CONFIG:Release>:NDEBUG>
)

# Note: _FILE_OFFSET_BITS=64 is not set as INTERFACE to avoid conflicts with JUCE's internal zlib
# JSFX sources will compile with whatever the consuming project uses
# Note: EEL2 assembly optimizations are configured via cmake/EEL2Assembly.cmake

# Set compile flags similar to Makefile
if(MSVC)
    # Apply warning suppressions only to C/CXX, not to assembly files
    target_compile_options(jsfx PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:/wd4100>
        $<$<COMPILE_LANGUAGE:C,CXX>:/wd4244>
        $<$<COMPILE_LANGUAGE:C,CXX>:/wd4245>
        $<$<COMPILE_LANGUAGE:C,CXX>:/wd4505>
        $<$<COMPILE_LANGUAGE:C,CXX>:/wd4456>
        $<$<COMPILE_LANGUAGE:C,CXX>:/wd4457>
        $<$<COMPILE_LANGUAGE:C,CXX>:/wd4458>
        $<$<COMPILE_LANGUAGE:C,CXX>:/wd4996>  # deprecated functions
    )
else()
    # Apply compiler flags only to C/CXX files, not to assembly files
    target_compile_options(jsfx PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error>
        $<$<COMPILE_LANGUAGE:C,CXX>:-fvisibility=hidden>
        $<$<COMPILE_LANGUAGE:C,CXX>:-fstrict-aliasing>
        $<$<COMPILE_LANGUAGE:C,CXX>:-fno-math-errno>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wtype-limits>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-parameter>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-sign-compare>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-function>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-shift-negative-value>
        $<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<NOT:$<PLATFORM_ID:Darwin>>>:-Wno-format-truncation>
        $<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<PLATFORM_ID:Darwin>>:-Wno-deprecated>
        $<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<PLATFORM_ID:Darwin>>:-Wno-switch>
        $<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<PLATFORM_ID:Darwin>>:-Wno-c++11-narrowing>
        # ARM architectures default to unsigned char, but WDL expects signed char
        $<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<OR:$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},aarch64>,$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},arm>>>:-fsigned-char>
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
        $<BUILD_INTERFACE:${jsfx_SOURCE_DIR}/WDL/zlib>  # For zlib support
        $<BUILD_INTERFACE:${jsfx_SOURCE_DIR}/WDL/libpng>  # For PNG support
        $<BUILD_INTERFACE:${jsfx_SOURCE_DIR}/WDL/jpeglib>  # For JPEG support
        $<BUILD_INTERFACE:${jsfx_SOURCE_DIR}/WDL/giflib>  # For GIF support
    PRIVATE
        ${jsfx_SOURCE_DIR}/jmde
)

# These were INTERFACE, but now we only need to expose the public API
# WDL_NO_DEFINE_MINMAX is already in PUBLIC compile definitions above
target_compile_definitions(jsfx PUBLIC
    LOCALIZE_DISABLE=1
    LOCALIZE_NO_DIALOG_MENU_REDEF=1
)
