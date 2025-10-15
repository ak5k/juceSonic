#pragma once

#include <jsfx.h>
#include <juce_core/juce_core.h>

extern jsfxAPI JesusonicAPI;

/**
 * Utility class for common JSFX parameter operations.
 * Eliminates redundancy across parameter validation, range conversion, and type detection.
 */
class ParameterUtils
{
public:
    enum class ParameterType
    {
        Boolean, // 0.0-1.0 with step=1.0, not enum
        Enum,    // Discrete choice parameter
        Integer, // Integer range with step>=1.0
        Float    // Continuous floating point
    };

    /**
     * Check if parameter index is valid for the given JSFX instance
     */
    static bool isValidParameterIndex(SX_Instance* instance, int index, int maxActiveParams);

    /**
     * Convert normalized parameter value (0.0-1.0) to actual JSFX parameter value
     */
    static double normalizedToActualValue(SX_Instance* instance, int paramIndex, float normalizedValue);

    /**
     * Convert actual JSFX parameter value to normalized value (0.0-1.0)
     */
    static float actualToNormalizedValue(SX_Instance* instance, int paramIndex, double actualValue);

    /**
     * Get parameter range information (min, max, step)
     */
    static bool getParameterRange(SX_Instance* instance, int paramIndex, double& minVal, double& maxVal, double& step);

    /**
     * Detect parameter type based on range and properties
     */
    static ParameterType detectParameterType(SX_Instance* instance, int paramIndex);

    /**
     * Get parameter name with fallback
     */
    static juce::String getParameterName(SX_Instance* instance, int paramIndex);

    /**
     * Get parameter display text for a given value
     */
    static juce::String getParameterDisplayText(SX_Instance* instance, int paramIndex, double value);

    /**
     * Check if parameter has changed beyond threshold
     */
    static bool hasParameterChanged(double newValue, double oldValue, double threshold = 0.0001);
};