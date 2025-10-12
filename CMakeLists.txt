# Example Audio Plugin CMakeLists.txt

# To get started on a new plugin, copy this entire folder (containing this file and C++ sources) to
# a convenient location, and then start making modifications.

# The first line of any CMake project should be a call to `cmake_minimum_required`, which checks
# that the installed CMake will be able to understand the following CMakeLists, and ensures that
# CMake's behaviour is compatible with the named version. This is a standard CMake command, so more
# information can be found in the CMake docs.

cmake_minimum_required(VERSION 3.22)

# The top-level CMakeLists.txt file for a project must contain a literal, direct call to the
# `project()` command. `project()` sets up some helpful variables that describe source/binary
# directories, and the current project version. This is a standard CMake command.

project(juceSonic VERSION 0.0.1)

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
    # VERSION ...                               # Set this if the plugin version is different to the project version
    # ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    # COMPANY_NAME ...                          # Specify the name of the plugin's author
    # IS_SYNTH TRUE/FALSE                       # Is this a synth or an effect?
    # NEEDS_MIDI_INPUT TRUE/FALSE               # Does the plugin need midi input?
    # NEEDS_MIDI_OUTPUT TRUE/FALSE              # Does the plugin need midi output?
    # IS_MIDI_EFFECT TRUE/FALSE                 # Is this plugin a MIDI effect?
    # EDITOR_WANTS_KEYBOARD_FOCUS TRUE/FALSE    # Does the editor need keyboard focus?
    COPY_PLUGIN_AFTER_BUILD TRUE                # Should the plugin be installed to a default location after building?
    PLUGIN_MANUFACTURER_CODE Juce               # A four-character manufacturer id with at least one upper-case character
    PLUGIN_CODE Dem0                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case

    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE

    FORMATS AU VST3 Standalone                  # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
    PRODUCT_NAME ${PROJECT_NAME})        # The name of the final executable, which can differ from the target name
    

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

target_sources(${PROJECT_NAME}
    PRIVATE
        PluginEditor.cpp
        PluginProcessor.cpp)

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

# juce_add_binary_data(AudioPluginData SOURCES ...)

# `target_link_libraries` links libraries and JUCE modules to other libraries or executables. Here,
# we're linking our executable target to the `juce::juce_audio_utils` module. Inter-module
# dependencies are resolved automatically, so `juce_core`, `juce_events` and so on will also be
# linked automatically. If we'd generated a binary data target above, we would need to link to it
# here too. This is a standard CMake command.

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        # AudioPluginData           # If we'd created a binary data target, we'd link to it here
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Reset jsfx submodule to HEAD and apply patch (adds Windows headers, removes DLL exports)
# message(STATUS "Resetting jsfx submodule...")
# Reset jsfx submodule to HEAD and apply patch (adds Windows headers, removes DLL exports)
message(STATUS "Resetting jsfx submodule...")
execute_process(
    COMMAND git reset --hard 
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/jsfx
    RESULT_VARIABLE RESET_RESULT
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT RESET_RESULT EQUAL 0)
    message(WARNING "Failed to reset jsfx submodule")
endif()

message(STATUS "Applying jsfx.patch...")
execute_process(
    COMMAND git apply ${CMAKE_CURRENT_SOURCE_DIR}/jsfx.patch
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/jsfx
    RESULT_VARIABLE PATCH_APPLY_RESULT
)
if(NOT PATCH_APPLY_RESULT EQUAL 0)
    message(WARNING "Failed to apply jsfx.patch")
endif()

add_library(lice STATIC)
target_include_directories(lice
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/lice/glew/include"
    )
file(GLOB LICE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/lice/glew/src/glew.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/lice/glew/src/glewinfo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/lice/*.cpp"
)
target_sources(lice PRIVATE ${LICE_SOURCES})
target_link_libraries(lice
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        )

add_library(jsfx OBJECT)
target_include_directories(jsfx
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/lice/glew/include"
    )
file(GLOB JSFX_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/eel2/nseel-*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/fft.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/localize/localize.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/win32_curses/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/wingui/wndsize.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/jsfx/*.cpp"
    )
target_link_libraries(jsfx
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        )
target_link_libraries(jsfx PRIVATE lice)

target_sources(jsfx PRIVATE ${JSFX_SOURCES})
if(APPLE)
    target_link_libraries(jsfx PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/eel2/asm-nseel-multi-macho.o")
else()
    target_link_libraries(jsfx PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/WDL/eel2/asm-nseel-x64.obj")
endif()

if(UNIX)
    target_compile_options(jsfx PRIVATE -fPIC)
    set_target_properties(jsfx PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(lice PRIVATE -fPIC)
    set_target_properties(lice PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_definitions(jsfx
    PRIVATE
        WDL_FFT_REALSIZE=8
        FORCE_WIN32_CURSES
)

set(MY_MSVC_WARNINGS
    /wd4018
    /wd4100
    /wd4127
    /wd4244
    /wd4245
    /wd4267
    /wd4273
    /wd4456
    /wd4505
    /wd4701
    /wd4702
    /wd4996 
    /wd4838
    /wd4311
    /wd5055
    /wd4273
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE ${MY_MSVC_WARNINGS})
    target_compile_options(lice PRIVATE ${MY_MSVC_WARNINGS})
    target_compile_options(jsfx PRIVATE ${MY_MSVC_WARNINGS}) # Uncomment if jsfx is enabled
endif()

target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/jsfx"
    )
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        jsfx
)

# Add JSFX UI resources directly to each plugin format target (resources can't be in static libraries)
if(WIN32)
    foreach(target_name IN ITEMS ${PROJECT_NAME}_VST3 ${PROJECT_NAME}_Standalone ${PROJECT_NAME}_AU)
        if(TARGET ${target_name})
            target_sources(${target_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/jsfx/res.rc")
        endif()
    endforeach()
endif()