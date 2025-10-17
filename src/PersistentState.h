#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Base class for components that need to persist state in APVTS.
 * Handles automatic per-JSFX state isolation using file path hashing.
 *
 * Usage:
 * 1. Inherit from this class
 * 2. Call setStateTree() with the APVTS state in constructor
 * 3. Use getStateProperty() and setStateProperty() for state access
 *
 * State keys are automatically prefixed with JSFX file path hash,
 * ensuring each loaded JSFX maintains independent state.
 */
class PersistentState
{
public:
    virtual ~PersistentState() = default;

    /**
     * Set the state tree to use for persistence.
     * Must be called before using getStateProperty/setStateProperty.
     */
    void setStateTree(juce::ValueTree& stateTree)
    {
        state = &stateTree;
    }

protected:
    /**
     * Get a state property with automatic per-JSFX scoping.
     * @param key Base property key (will be prefixed with JSFX hash)
     * @param defaultValue Default value if property doesn't exist
     * @return Property value
     */
    template <typename T>
    T getStateProperty(const juce::String& key, const T& defaultValue) const
    {
        if (!state)
            return defaultValue;

        return state->getProperty(getScopedKey(key), defaultValue);
    }

    /**
     * Set a state property with automatic per-JSFX scoping.
     * @param key Base property key (will be prefixed with JSFX hash)
     * @param value Value to set
     */
    template <typename T>
    void setStateProperty(const juce::String& key, const T& value)
    {
        if (!state)
            return;

        state->setProperty(getScopedKey(key), value, nullptr);
    }

    /**
     * Get a global property (not per-JSFX scoped).
     * Use this for truly global settings.
     */
    template <typename T>
    T getGlobalProperty(const juce::String& key, const T& defaultValue) const
    {
        if (!state)
            return defaultValue;

        return state->getProperty(key, defaultValue);
    }

    /**
     * Set a global property (not per-JSFX scoped).
     * Use this for truly global settings.
     */
    template <typename T>
    void setGlobalProperty(const juce::String& key, const T& value)
    {
        if (!state)
            return;

        state->setProperty(key, value, nullptr);
    }

    /**
     * Get the current JSFX file path for state scoping.
     */
    juce::String getCurrentJsfxPath() const
    {
        if (!state)
            return {};

        return state->getProperty("jsfxFilePath", "").toString();
    }

private:
    /**
     * Create a scoped property key using JSFX file path hash.
     * This ensures each JSFX has independent state.
     */
    juce::String getScopedKey(const juce::String& baseKey) const
    {
        auto jsfxPath = getCurrentJsfxPath();
        if (jsfxPath.isEmpty())
            return baseKey; // Fallback to global key if no JSFX loaded

        // Use hash of file path to create shorter, unique key
        auto hash = juce::String(jsfxPath.hashCode64());
        return baseKey + "_" + hash;
    }

    juce::ValueTree* state = nullptr;
};
