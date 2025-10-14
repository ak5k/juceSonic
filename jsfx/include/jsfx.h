/*
 * JSFX API Header
 *
 * This header provides a clean interface to the JSFX library.
 */

// clang-format off

#ifndef JSFX_H
#define JSFX_H

// On Windows, include Windows headers first to ensure proper Win32 API definitions
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

#include "sfxui.h"
#include "jsfx_api.h"

#endif

// clang-format on