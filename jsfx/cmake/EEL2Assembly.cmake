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
set(NASM_EXECUTABLE "" CACHE INTERNAL "NASM executable path")

# Validate architecture
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Pointer size: ${CMAKE_SIZEOF_VOID_P} bytes")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "EEL2: Only x64 (64-bit) architecture is supported. Current pointer size: ${CMAKE_SIZEOF_VOID_P} bytes")
endif()

# Set assembly source path
set(EEL2_ASM_SOURCE "${jsfx_SOURCE_DIR}/WDL/eel2/asm-nseel-x64-sse.asm")

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
# Find or Download NASM
# ==============================================================================
if(WIN32)
    # On Windows, always download NASM to ensure consistent version
    message(STATUS "EEL2: Windows detected - downloading NASM for consistent build environment")
    download_and_setup_nasm()
    
    if(NOT NASM_FOUND)
        message(FATAL_ERROR "EEL2: Failed to download NASM")
    endif()
else()
    # On non-Windows platforms, use system NASM
    find_program(NASM_EXECUTABLE nasm
        DOC "NASM assembler executable"
    )
    
    if(NASM_EXECUTABLE)
        message(STATUS "EEL2: Found system NASM at ${NASM_EXECUTABLE}")
        set(NASM_FOUND TRUE)
    else()
        message(FATAL_ERROR "EEL2: NASM is required but not found. Install with:\n  Ubuntu/Debian: sudo apt-get install nasm\n  macOS: brew install nasm\n  Arch: sudo pacman -S nasm")
    endif()
endif()

if(NOT NASM_EXECUTABLE OR NOT EXISTS "${NASM_EXECUTABLE}")
    message(FATAL_ERROR "EEL2: NASM executable not found")
endif()

message(STATUS "EEL2: Using NASM: ${NASM_EXECUTABLE}")

# ==============================================================================
# Configure NASM
# ==============================================================================
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

set(CMAKE_ASM_NASM_FLAGS "" CACHE STRING "NASM flags")
set(EEL2_HAS_ASM TRUE CACHE INTERNAL "Assembly available")

message(STATUS "EEL2: Object format: ${CMAKE_ASM_NASM_OBJECT_FORMAT}")
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
    
    set_source_files_properties(${EEL2_ASM_SOURCE}
        PROPERTIES LANGUAGE ASM_NASM
    )
    
    message(STATUS "EEL2: Assembly added to target '${target}'")
endfunction()
