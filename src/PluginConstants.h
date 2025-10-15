#pragma once

/**
 * Central location for plugin-wide constants
 */
namespace PluginConstants
{
// Maximum number of parameters supported by the plugin
static constexpr int MaxParameters = 256;

// Parameter smoothing time in milliseconds
static constexpr double ParameterSmoothingMs = 20.0;

// Maximum number of channels supported by the plugin
static constexpr int MaxChannels = 64;

// Maximum number of channels supported by JSFX backend
static constexpr int JsfxMaxChannels = 128;
} // namespace PluginConstants
