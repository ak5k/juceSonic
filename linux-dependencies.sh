#!/bin/bash

# ==============================================================================
# Linux dependencies for juceSonic
# ==============================================================================
#
# This file defines the required apt packages for building the project.
# 
# Usage:
#   1. Direct installation: ./linux-dependencies.sh
#   2. Source in scripts: source linux-dependencies.sh
#      Then access via: "${DEPENDENCIES[@]}"
#   3. Used automatically by: cross-compile.sh
#
# ==============================================================================

# Array of dependencies (without architecture suffix)
# The cross-compile script will append :arch as needed
DEPENDENCIES=(
    # Audio libraries
    "libasound2-dev"
    "libjack-jackd2-dev"
    "ladspa-sdk"
    
    # Network libraries
    "libcurl4-openssl-dev"
    
    # Font libraries
    "libfreetype-dev"
    "libfontconfig1-dev"
    
    # X11 libraries
    "libx11-dev"
    "libxcomposite-dev"
    "libxcursor-dev"
    "libxext-dev"
    "libxinerama-dev"
    "libxrandr-dev"
    "libxrender-dev"
    
    # GTK and WebKit
    "libgtk-3-dev"
    "libgtk-*-dev"
    "libwebkit2gtk-*-dev"
    
    # OpenGL/Mesa
    "libglu1-mesa-dev"
    "mesa-common-dev"

    # Assembler for optimized builds
    nasm
)

# Export the array so it can be sourced by other scripts
export DEPENDENCIES
