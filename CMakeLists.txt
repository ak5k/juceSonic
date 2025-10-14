# Example Audio Plugin CMakeLists.txt

# To get started on a new plugin, copy this entire folder (containing this file and C++ sources) to
# a convenient location, and then start making modifications.

# The first line of any CMake project should be a call to `cmake_minimum_required`, which checks
# that the installed CMake will be able to understand the following CMakeLists, and ensures that
# CMake's behaviour is compatible with the named version. This is a standard CMake command, so more
# information can be found in the CMake docs.

cmake_minimum_required(VERSION 4.1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The top-level CMakeLists.txt file for a project must contain a literal, direct call to the
# `project()` command. `project()` sets up some helpful variables that describe source/binary
# directories, and the current project version. This is a standard CMake command.

project(juceSonic VERSION 0.0.1 LANGUAGES CXX C)

# ==============================================================================
# Submodule and Dependency Initialization (runs only on first successful configure)
# ==============================================================================

# Check if initialization has been completed (cache variable)
if(NOT SUBMODULES_INITIALIZED)
    message(STATUS "Running first-time initialization...")
    
    set(INIT_SUCCESS TRUE)
    
    # Step 1: Git submodule initialization
    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
        message(STATUS "Initializing git submodules...")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE GIT_SUBMOD_RESULT
        )
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}")
            set(INIT_SUCCESS FALSE)
        else()
            message(STATUS "Git submodules initialized successfully")
            
            # Checkout submodules to their default branches (main or master) instead of detached HEAD
            message(STATUS "Checking out submodules to their default branches...")
            execute_process(
                COMMAND ${GIT_EXECUTABLE} submodule foreach "git checkout main 2>/dev/null || git checkout master 2>/dev/null || echo 'Could not checkout default branch for' $name"
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                RESULT_VARIABLE SUBMODULE_CHECKOUT_RESULT
                OUTPUT_VARIABLE SUBMODULE_CHECKOUT_OUTPUT
            )
            if(SUBMODULE_CHECKOUT_RESULT EQUAL "0")
                message(STATUS "Submodules checked out to their default branches")
                message(STATUS "${SUBMODULE_CHECKOUT_OUTPUT}")
            else()
                message(WARNING "Some submodules may not have been checked out to their default branches")
            endif()
        endif()
    else()
        message(WARNING "Git not found or not a git repository. Submodules will not be initialized automatically.")
        set(INIT_SUCCESS FALSE)
    endif()
    
    # Step 2: Additional initialization commands will go here
    # Example:
    # if(INIT_SUCCESS)
    #     message(STATUS "Running additional initialization step...")
    #     execute_process(...)
    #     if(NOT RESULT EQUAL "0")
    #         set(INIT_SUCCESS FALSE)
    #     endif()
    # endif()
    
    # Only mark as initialized if ALL steps succeeded
    if(INIT_SUCCESS)
        set(SUBMODULES_INITIALIZED TRUE CACHE BOOL "All initialization steps completed successfully" FORCE)
        message(STATUS "All initialization steps completed successfully")
    else()
        message(WARNING "Initialization incomplete. Will retry on next configure.")
    endif()
else()
    message(STATUS "Initialization already completed (SUBMODULES_INITIALIZED=${SUBMODULES_INITIALIZED})")
endif()

# If you've installed JUCE somehow (via a package manager, or directly using the CMake install
# target), you'll need to tell this project that it depends on the installed copy of JUCE. If you've
# included JUCE directly in your source tree (perhaps as a submodule), you'll need to tell CMake to
# include that subdirectory as part of the build.

# find_package(JUCE CONFIG REQUIRED)        # If you've installed JUCE to your system
# or
add_subdirectory(JUCE)                    # If you've put JUCE in a subdirectory called JUCE

# If you are building a VST2 or AAX plugin, CMake needs to be told where to find these SDKs on your
# system. This setup should be done before calling `juce_add_plugin`.

# juce_set_vst2_sdk_path(...)
# juce_set_aax_sdk_path(...)

# `juce_add_plugin` adds a static library target with the name passed as the first argument
# (AudioPluginExample here). This target is a normal CMake target, but has a lot of extra properties set
# up by default. As well as this shared code static library, this function adds targets for each of
# the formats specified by the FORMATS arguments. This function accepts many optional arguments.
# Check the readme at `docs/CMake API.md` in the JUCE repo for the full list.

juce_add_plugin(${PROJECT_NAME}
    COMPANY_NAME "ak5k"
    COMPANY_WEBSITE "https://github.com/ak5k"
    COMPANY_EMAIL ""
    
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Ak5k               # A four-character manufacturer id with at least one upper-case character
    PLUGIN_CODE Jsfx                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case

    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE

    VST3_CATEGORIES Fx
    
    FORMATS AU VST3 Standalone
    PRODUCT_NAME ${PROJECT_NAME})
    

# `juce_generate_juce_header` will create a JuceHeader.h for a given target, which will be generated
# into your build tree. This should be included with `#include <JuceHeader.h>`. The include path for
# this header will be automatically added to the target. The main function of the JuceHeader is to
# include all your JUCE module headers; if you're happy to include module headers directly, you
# probably don't need to call this.

# juce_generate_juce_header(AudioPluginExample)

# `target_sources` adds source files to a target. We pass the target that needs the sources as the
# first argument, then a visibility parameter for the sources which should normally be PRIVATE.
# Finally, we supply a list of source files that will be built into the target. This is a standard
# CMake command.

file(GLOB_RECURSE SOURCE_FILES src/*.cpp src/*.h)
target_sources(${PROJECT_NAME} PRIVATE ${SOURCE_FILES})

# `target_compile_definitions` adds some preprocessor definitions to our target. In a Projucer
# project, these might be passed in the 'Preprocessor Definitions' field. JUCE modules also make use
# of compile definitions to switch certain features on/off, so if there's a particular feature you
# need that's not on by default, check the module header for the correct flag to set here. These
# definitions will be visible both to your code, and also the JUCE module code, so for new
# definitions, pick unique names that are unlikely to collide! This is a standard CMake command.

target_compile_definitions(${PROJECT_NAME}
    PUBLIC
        # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
        JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
        JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
        JUCE_VST3_CAN_REPLACE_VST2=0)

# If your target needs extra binary assets, you can add them here. The first argument is the name of
# a new static library target that will include all the binary resources. There is an optional
# `NAMESPACE` argument that can specify the namespace of the generated binary data class. Finally,
# the SOURCES argument should be followed by a list of source files that should be built into the
# static library. These source files can be of any kind (wav data, images, fonts, icons etc.).
# Conversion to binary-data will happen when your target is built.

# `target_link_libraries` links libraries and JUCE modules to other libraries or executables. Here,
# we're linking our executable target to the `juce::juce_audio_utils` module. Inter-module
# dependencies are resolved automatically, so `juce_core`, `juce_events` and so on will also be
# linked automatically. If we'd generated a binary data target above, we would need to link to it
# here too. This is a standard CMake command.

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Disable specific warnings for JSFX integration
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /wd4099
        /wd4100  # unreferenced formal parameter
        /wd4244
        /wd4458
        /wd4505  # unreferenced local function has been removed
        /wd4996
    )
endif()


# ==============================================================================
# JSFX Integration - Using jsfx-cmake submodule
# ==============================================================================

# Add jsfx-cmake submodule (handles all WDL/SWELL/curses complexity)
set(JSFX_BUILD_EXAMPLES OFF CACHE BOOL "Don't build jsfx examples" FORCE)
add_subdirectory(jsfx-cmake)

# On Windows, explicitly add JSFX resource file to the plugin targets
# This ensures the dialog templates are compiled into the VST3/Standalone DLL
if(WIN32)
    # The jsfx source directory is in build/_deps after FetchContent
    set(JSFX_RES_FILE "${CMAKE_CURRENT_BINARY_DIR}/_deps/jsfx-src/jsfx/res.rc")
    set(JSFX_RES_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/_deps/jsfx-src/jsfx")
    
    # Add to SharedCode library
    target_sources(${PROJECT_NAME}
        PRIVATE
            ${JSFX_RES_FILE}
    )
    target_include_directories(${PROJECT_NAME}
        PRIVATE
            ${JSFX_RES_INCLUDE}
    )
    
    # Add to format-specific targets (VST3, Standalone) after juce_add_plugin creates them
    # These are named ${PROJECT_NAME}_VST3, ${PROJECT_NAME}_Standalone, etc.
    foreach(target ${PROJECT_NAME}_VST3 ${PROJECT_NAME}_Standalone)
        if(TARGET ${target})
            target_sources(${target}
                PRIVATE
                    ${JSFX_RES_FILE}
            )
            target_include_directories(${target}
                PRIVATE
                    ${JSFX_RES_INCLUDE}
            )
        endif()
    endforeach()
endif()

# Link JSFX to main plugin target
# Use linker groups to handle circular dependencies between our code and jsfx_core
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            -Wl,--start-group
            jsfx
            -Wl,--end-group
    )
else()
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            jsfx
    )
endif()
