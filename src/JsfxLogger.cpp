#include "JsfxLogger.h"

void JsfxLogger::info(const juce::String& message)
{
    log(Level::Info, JucePlugin_Name, message);
}

void JsfxLogger::info(const juce::String& component, const juce::String& message)
{
    log(Level::Info, component, message);
}

void JsfxLogger::debug(const juce::String& message)
{
#ifdef JUCE_DEBUG
    log(Level::Debug, JucePlugin_Name, message);
#else
    juce::ignoreUnused(message);
#endif
}

void JsfxLogger::debug(const juce::String& component, const juce::String& message)
{
#ifdef JUCE_DEBUG
    log(Level::Debug, component, message);
#else
    juce::ignoreUnused(component, message);
#endif
}

void JsfxLogger::warning(const juce::String& message)
{
    log(Level::Warning, JucePlugin_Name, message);
}

void JsfxLogger::warning(const juce::String& component, const juce::String& message)
{
    log(Level::Warning, component, message);
}

void JsfxLogger::error(const juce::String& message)
{
    log(Level::Error, JucePlugin_Name, message);
}

void JsfxLogger::error(const juce::String& component, const juce::String& message)
{
    log(Level::Error, component, message);
}

void JsfxLogger::logParameterChange(
    int paramIndex,
    float normalizedValue,
    double actualValue,
    double minVal,
    double maxVal
)
{
#ifdef JUCE_DEBUG
    juce::String message = "Param "
                         + juce::String(paramIndex)
                         + ": normalized="
                         + juce::String(normalizedValue, 3)
                         + " actual="
                         + juce::String(actualValue, 3)
                         + " range=["
                         + juce::String(minVal, 3)
                         + ".."
                         + juce::String(maxVal, 3)
                         + "]";
    debug("Parameters", message);
#else
    juce::ignoreUnused(paramIndex, normalizedValue, actualValue, minVal, maxVal);
#endif
}

void JsfxLogger::logInstanceLifecycle(const juce::String& event, const juce::String& details)
{
    juce::String message = event;
    if (details.isNotEmpty())
        message += " - " + details;
    debug("Lifecycle", message);
}

void JsfxLogger::log(Level level, const juce::String& component, const juce::String& message)
{
    juce::String formatted = formatMessage(level, component, message);

    // Use JUCE's DBG for actual output (it handles debug vs release builds)
    DBG(formatted);
}

juce::String JsfxLogger::formatMessage(Level level, const juce::String& component, const juce::String& message)
{
    juce::String levelStr;
    switch (level)
    {
    case Level::Debug:
        levelStr = "DEBUG";
        break;
    case Level::Info:
        levelStr = "INFO";
        break;
    case Level::Warning:
        levelStr = "WARN";
        break;
    case Level::Error:
        levelStr = "ERROR";
        break;
    }

    return "[" + levelStr + "] " + component + ": " + message;
}