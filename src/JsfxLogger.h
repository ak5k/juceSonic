#pragma once

#include <juce_core/juce_core.h>

/**
 * Centralized logging utility for JuceSonic with consistent prefixes and levels.
 * Replaces scattered DBG statements throughout the codebase.
 */
class JsfxLogger
{
public:
    enum class Level
    {
        Debug,
        Info,
        Warning,
        Error
    };

    /**
     * Log an informational message
     */
    static void info(const juce::String& message);

    /**
     * Log an informational message with component context
     */
    static void info(const juce::String& component, const juce::String& message);

    /**
     * Log a debug message (only in debug builds)
     */
    static void debug(const juce::String& message);

    /**
     * Log a debug message with component context (only in debug builds)
     */
    static void debug(const juce::String& component, const juce::String& message);

    /**
     * Log a warning message
     */
    static void warning(const juce::String& message);

    /**
     * Log a warning message with component context
     */
    static void warning(const juce::String& component, const juce::String& message);

    /**
     * Log an error message
     */
    static void error(const juce::String& message);

    /**
     * Log an error message with component context
     */
    static void error(const juce::String& component, const juce::String& message);

    /**
     * Log parameter change information (debug level)
     */
    static void
    logParameterChange(int paramIndex, float normalizedValue, double actualValue, double minVal, double maxVal);

    /**
     * Log JSFX instance lifecycle events
     */
    static void logInstanceLifecycle(const juce::String& event, const juce::String& details = {});

private:
    static void log(Level level, const juce::String& component, const juce::String& message);
    static juce::String formatMessage(Level level, const juce::String& component, const juce::String& message);
};