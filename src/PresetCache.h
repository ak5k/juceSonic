#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

/**
 * @brief In-memory cache for JSFX presets
 *
 * Stores preset library data in memory without persisting to APVTS/project files.
 * PresetLoader populates this cache in background, PresetWindow reads from it.
 *
 * Thread-safe: Uses read-write lock for concurrent access.
 */
class PresetCache
{
public:
    PresetCache() = default;
    ~PresetCache() = default;

    /**
     * @brief Update the entire preset cache with new data
     * Called by PresetLoader after scanning preset files
     */
    void updateCache(const juce::ValueTree& newPresetsTree);

    /**
     * @brief Get a copy of the current preset tree
     * Returns a copy to avoid threading issues
     */
    juce::ValueTree getPresetsTree() const;

    /**
     * @brief Clear all cached presets
     */
    void clear();

    /**
     * @brief Check if cache has any presets
     */
    bool isEmpty() const;

    /**
     * @brief Get number of preset files in cache
     */
    int getNumFiles() const;

    /**
     * @brief Callback invoked when cache is updated
     * Listeners can register to be notified of cache changes
     */
    std::function<void()> onCacheUpdated;

private:
    juce::ValueTree presetsTree{"presets"};
    mutable juce::ReadWriteLock lock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetCache)
};
