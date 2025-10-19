# ==============================================================================
# EEL2 Assembly Optimization Support
# ==============================================================================
# This module handles cross-platform assembly compilation for EEL2
# - Automatically downloads NASM on Windows if not found
# - Uses NASM for assembly compilation on all platforms
# - FATAL ERROR if assembly cannot be set up (no portable fallback)
# ==============================================================================

cmake_minimum_required(VERSION 3.14)

message(STATUS "========================================")
message(STATUS "EEL2 Assembly Configuration")
message(STATUS "========================================")

# Cache variables
set(EEL2_HAS_ASM FALSE CACHE INTERNAL "Whether assembly optimizations are available")
set(EEL2_ASM_SOURCE "" CACHE INTERNAL "Assembly source file to compile")
set(EEL2_USE_GCC_ASM FALSE CACHE INTERNAL "Whether using GCC inline assembly")
set(NASM_EXECUTABLE "" CACHE INTERNAL "NASM executable path")

# Validate architecture
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Pointer size: ${CMAKE_SIZEOF_VOID_P} bytes")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "EEL2: Only 64-bit architectures are supported. Current pointer size: ${CMAKE_SIZEOF_VOID_P} bytes")
endif()

# Detect target architecture
set(IS_ARM64 FALSE)
set(IS_X64 FALSE)
set(IS_MACOS_UNIVERSAL FALSE)

# Check for macOS universal binary build
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES NUM_ARCHS)
    if(NUM_ARCHS GREATER 1)
        set(IS_MACOS_UNIVERSAL TRUE)
        message(STATUS "EEL2: Target architecture: macOS Universal Binary (${CMAKE_OSX_ARCHITECTURES})")
    endif()
endif()

if(NOT IS_MACOS_UNIVERSAL)
    # Check CMAKE_GENERATOR_PLATFORM first (set by -A flag or Visual Studio generator)
    if(CMAKE_GENERATOR_PLATFORM MATCHES "^(ARM64|arm64)$")
        set(IS_ARM64 TRUE)
        message(STATUS "EEL2: Target architecture: ARM64 (from CMAKE_GENERATOR_PLATFORM)")
    elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^(x64|X64|Win64)$")
        set(IS_X64 TRUE)
        message(STATUS "EEL2: Target architecture: x64 (from CMAKE_GENERATOR_PLATFORM)")
    # Fall back to CMAKE_SYSTEM_PROCESSOR
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|ARM64|arm64)$")
        set(IS_ARM64 TRUE)
        message(STATUS "EEL2: Target architecture: ARM64 (from CMAKE_SYSTEM_PROCESSOR)")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$")
        set(IS_X64 TRUE)
        message(STATUS "EEL2: Target architecture: x64 (from CMAKE_SYSTEM_PROCESSOR)")
    else()
        # Default to x64 if we can't detect
        set(IS_X64 TRUE)
        message(WARNING "EEL2: Could not detect architecture, defaulting to x64")
    endif()
endif()

# Select appropriate assembly source based on architecture
if(APPLE)
    # macOS always uses pre-compiled universal binary (ARM64 + x64)
    set(EEL2_ASM_SOURCE "${jsfx_SOURCE_DIR}/WDL/eel2/asm-nseel-multi-macho.o")
    set(EEL2_USE_PRECOMPILED TRUE)
    message(STATUS "EEL2: Using pre-compiled universal binary object for macOS")
elseif(IS_ARM64 AND WIN32)
    # ARM64 Windows - use pre-compiled object to avoid armasm64 toolchain issues
    set(EEL2_ASM_SOURCE "${jsfx_SOURCE_DIR}/WDL/eel2/asm-nseel-aarch64-msvc.obj")
    set(EEL2_USE_PRECOMPILED TRUE)
    message(STATUS "EEL2: Using pre-compiled ARM64 object for Windows")
elseif(IS_ARM64)
    # ARM64 on Linux - use GCC inline assembly (C file, not NASM)
    set(EEL2_ASM_SOURCE "${jsfx_SOURCE_DIR}/WDL/eel2/asm-nseel-aarch64-gcc.c")
    set(EEL2_USE_PRECOMPILED FALSE)
    set(EEL2_USE_GCC_ASM TRUE)
    message(STATUS "EEL2: Using ARM64 GCC inline assembly for ${CMAKE_SYSTEM_NAME}")
else()
    # x64 Windows/Linux - compile from source
    set(EEL2_ASM_SOURCE "${jsfx_SOURCE_DIR}/WDL/eel2/asm-nseel-x64-sse.asm")
    set(EEL2_USE_PRECOMPILED FALSE)
    message(STATUS "EEL2: Using x64 assembly")
endif()

if(NOT EXISTS "${EEL2_ASM_SOURCE}")
    message(FATAL_ERROR "EEL2: Assembly source file not found: ${EEL2_ASM_SOURCE}")
endif()

message(STATUS "Assembly source: ${EEL2_ASM_SOURCE}")

# ==============================================================================
# NASM Download and Setup
# ==============================================================================
function(download_and_setup_nasm)
    # Fetch latest NASM version from releases page
    message(STATUS "EEL2: Fetching latest NASM version...")
    
    set(NASM_RELEASES_URL "https://www.nasm.us/pub/nasm/releasebuilds/")
    file(DOWNLOAD "${NASM_RELEASES_URL}" "${CMAKE_BINARY_DIR}/nasm_releases.html"
        STATUS download_status
        TLS_VERIFY ON
    )
    
    list(GET download_status 0 status_code)
    if(NOT status_code EQUAL 0)
        # Fallback to known stable version
        message(WARNING "EEL2: Could not fetch NASM releases list, using fallback version 3.01")
        set(NASM_VERSION "3.01")
    else()
        # Parse HTML to find latest stable version (format: X.YY or X.YY.ZZ)
        file(READ "${CMAKE_BINARY_DIR}/nasm_releases.html" NASM_HTML)
        
        # Extract version directories, excluding RC versions
        string(REGEX MATCHALL "href=\"([0-9]+\\.[0-9]+(\\.[0-9]+)?)/\"" NASM_VERSIONS "${NASM_HTML}")
        
        # Process matches to get clean version numbers
        set(LATEST_VERSION "3.01")
        set(LATEST_MAJOR 3)
        set(LATEST_MINOR 1)
        set(LATEST_PATCH 0)
        
        foreach(VERSION_MATCH ${NASM_VERSIONS})
            string(REGEX MATCH "([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" VERSION_NUM "${VERSION_MATCH}")
            if(CMAKE_MATCH_1 AND CMAKE_MATCH_2)
                set(CUR_MAJOR ${CMAKE_MATCH_1})
                set(CUR_MINOR ${CMAKE_MATCH_2})
                set(CUR_PATCH 0)
                if(CMAKE_MATCH_4)
                    set(CUR_PATCH ${CMAKE_MATCH_4})
                endif()
                
                # Compare versions
                if(CUR_MAJOR GREATER LATEST_MAJOR OR
                   (CUR_MAJOR EQUAL LATEST_MAJOR AND CUR_MINOR GREATER LATEST_MINOR) OR
                   (CUR_MAJOR EQUAL LATEST_MAJOR AND CUR_MINOR EQUAL LATEST_MINOR AND CUR_PATCH GREATER LATEST_PATCH))
                    set(LATEST_MAJOR ${CUR_MAJOR})
                    set(LATEST_MINOR ${CUR_MINOR})
                    set(LATEST_PATCH ${CUR_PATCH})
                    if(CUR_PATCH GREATER 0)
                        set(LATEST_VERSION "${CUR_MAJOR}.${CUR_MINOR}.${CUR_PATCH}")
                    else()
                        set(LATEST_VERSION "${CUR_MAJOR}.${CUR_MINOR}")
                    endif()
                endif()
            endif()
        endforeach()
        
        set(NASM_VERSION "${LATEST_VERSION}")
        message(STATUS "EEL2: Latest stable NASM version: ${NASM_VERSION}")
        
        # Clean up
        file(REMOVE "${CMAKE_BINARY_DIR}/nasm_releases.html")
    endif()
    
    set(NASM_URL "https://www.nasm.us/pub/nasm/releasebuilds/${NASM_VERSION}/win64/nasm-${NASM_VERSION}-win64.zip")
    set(NASM_DIR "${CMAKE_BINARY_DIR}/nasm-${NASM_VERSION}")
    set(NASM_EXE "${NASM_DIR}/nasm.exe")
    
    if(EXISTS "${NASM_EXE}")
        message(STATUS "EEL2: Found downloaded NASM ${NASM_VERSION} at ${NASM_EXE}")
        set(NASM_EXECUTABLE "${NASM_EXE}" PARENT_SCOPE)
        set(NASM_FOUND TRUE PARENT_SCOPE)
        return()
    endif()
    
    message(STATUS "EEL2: Downloading NASM ${NASM_VERSION}...")
    
    set(NASM_ZIP "${CMAKE_BINARY_DIR}/nasm-${NASM_VERSION}.zip")
    file(DOWNLOAD "${NASM_URL}" "${NASM_ZIP}"
        STATUS download_status
        SHOW_PROGRESS
        TLS_VERIFY ON
    )
    
    list(GET download_status 0 status_code)
    list(GET download_status 1 status_string)
    
    if(NOT status_code EQUAL 0)
        message(FATAL_ERROR "EEL2: Failed to download NASM ${NASM_VERSION}: ${status_string}")
    endif()
    
    message(STATUS "EEL2: Extracting NASM...")
    file(ARCHIVE_EXTRACT
        INPUT "${NASM_ZIP}"
        DESTINATION "${CMAKE_BINARY_DIR}"
    )
    
    if(EXISTS "${NASM_EXE}")
        message(STATUS "EEL2: NASM ${NASM_VERSION} successfully installed to ${NASM_DIR}")
        set(NASM_EXECUTABLE "${NASM_EXE}" PARENT_SCOPE)
        set(NASM_FOUND TRUE PARENT_SCOPE)
        file(REMOVE "${NASM_ZIP}")
    else()
        message(FATAL_ERROR "EEL2: NASM extraction failed - nasm.exe not found at ${NASM_EXE}")
    endif()
endfunction()

# ==============================================================================
# Find or Download NASM for source compilation, or use pre-compiled objects
# ==============================================================================
if(EEL2_USE_PRECOMPILED)
    # Pre-compiled object (macOS, Windows ARM64)
    message(STATUS "EEL2: Using pre-compiled assembly object - no assembler needed")
    set(EEL2_HAS_ASM TRUE CACHE INTERNAL "Assembly available")
    
elseif(EEL2_USE_GCC_ASM)
    # GCC inline assembly (Linux ARM64) - C file, no assembler needed
    message(STATUS "EEL2: Using GCC inline assembly - no assembler needed")
    set(EEL2_HAS_ASM TRUE CACHE INTERNAL "Assembly available")
    
elseif(WIN32)
    # Windows x64 - download NASM for consistent build environment
    message(STATUS "EEL2: Windows x64 - downloading NASM for consistent build environment")
    download_and_setup_nasm()
    
    if(NOT NASM_FOUND)
        message(FATAL_ERROR "EEL2: Failed to download NASM")
    endif()
    
    set(EEL2_USE_MASM FALSE)
else()
    # Non-Windows platforms: use 'which' to find NASM
    message(STATUS "EEL2: Searching for NASM...")
    
    execute_process(
        COMMAND which nasm
        OUTPUT_VARIABLE NASM_EXECUTABLE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE NASM_WHICH_RESULT
    )
    
    if(NASM_WHICH_RESULT EQUAL 0 AND EXISTS "${NASM_EXECUTABLE}")
        # Verify NASM works by checking its version
        execute_process(
            COMMAND ${NASM_EXECUTABLE} --version
            OUTPUT_VARIABLE NASM_VERSION_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE NASM_VERSION_RESULT
        )
        
        if(NASM_VERSION_RESULT EQUAL 0)
            message(STATUS "EEL2: Found system NASM at ${NASM_EXECUTABLE}")
            message(STATUS "EEL2: NASM version: ${NASM_VERSION_OUTPUT}")
            set(NASM_FOUND TRUE)
        else()
            message(FATAL_ERROR "EEL2: NASM found but doesn't execute properly: ${NASM_EXECUTABLE}")
        endif()
    else()
        message(FATAL_ERROR "EEL2: NASM is required but not found. Install with:\n  Ubuntu/Debian: sudo apt-get install nasm\n  macOS: brew install nasm\n  Arch: sudo pacman -S nasm")
    endif()
endif()

# Configure assembler (skip if using pre-compiled object)
if(NOT EEL2_USE_PRECOMPILED)
    # Configure NASM for source compilation
    if(NOT NASM_EXECUTABLE OR NOT EXISTS "${NASM_EXECUTABLE}")
        message(FATAL_ERROR "EEL2: NASM executable not found")
    endif()

    message(STATUS "EEL2: Using NASM: ${NASM_EXECUTABLE}")

    # IMPORTANT: Set compiler BEFORE enable_language
    set(CMAKE_ASM_NASM_COMPILER "${NASM_EXECUTABLE}" CACHE FILEPATH "NASM compiler" FORCE)

    enable_language(ASM_NASM)

    if(WIN32)
        set(CMAKE_ASM_NASM_OBJECT_FORMAT win64)
    elseif(APPLE)
        set(CMAKE_ASM_NASM_OBJECT_FORMAT macho64)
    elseif(UNIX)
        set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)
    else()
        message(FATAL_ERROR "EEL2: Unknown platform")
    endif()

    # Clear any inherited C/C++ compiler flags that NASM doesn't understand
    set(CMAKE_ASM_NASM_FLAGS "" CACHE STRING "NASM flags" FORCE)
    set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> <FLAGS> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>" CACHE STRING "NASM compile command" FORCE)
    
    message(STATUS "EEL2: Object format: ${CMAKE_ASM_NASM_OBJECT_FORMAT}")
endif()

set(EEL2_HAS_ASM TRUE CACHE INTERNAL "Assembly available")
message(STATUS "========================================")
message(STATUS "EEL2 Assembly: ENABLED")
message(STATUS "========================================")

# ==============================================================================
# Function to add assembly to target
# ==============================================================================
function(eel2_add_assembly_to_target target)
    if(NOT EEL2_HAS_ASM)
        message(FATAL_ERROR "EEL2: Assembly not available")
    endif()
    
    target_sources(${target} PRIVATE ${EEL2_ASM_SOURCE})
    
    if(EEL2_USE_PRECOMPILED)
        # Pre-compiled object file (.o or .obj)
        # No need to set language, just link it directly
        set_source_files_properties(${EEL2_ASM_SOURCE}
            PROPERTIES 
                EXTERNAL_OBJECT TRUE
                GENERATED TRUE
        )
        message(STATUS "EEL2: Pre-compiled assembly object added to target '${target}'")
    elseif(EEL2_USE_GCC_ASM)
        # GCC inline assembly (C file with asm blocks)
        # Treat as regular C file - no special properties needed
        message(STATUS "EEL2: GCC inline assembly source (C file) added to target '${target}'")
    else()
        # Source assembly file - compile with NASM
        set_source_files_properties(${EEL2_ASM_SOURCE}
            PROPERTIES LANGUAGE ASM_NASM
        )
        message(STATUS "EEL2: Assembly source added to target '${target}' (will be compiled with NASM)")
    endif()
endfunction()
