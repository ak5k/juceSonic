#pragma once

#include "ReaperPresetConverter.h"
#include <Config.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <memory>

/**
 * @brief Asynchronous preset loader with sole responsibility for loading presets
 *
 * Thread-safe class that:
 * - Loads presets from default locations in a background thread
 * - Parses .rpl files using ReaperPresetConverter
 * - Atomically updates APVTS state via message thread
 * - Can be triggered to refresh when needed
 *
 * Default locations searched:
 * 1. Same directory as current JSFX file (*.rpl)
 * 2. User-configured preset directories from APVTS
 * 3. REAPER Effects directory (recursive, filtered by JSFX name)
 *
 * Storage structure in APVTS.state:
 * ```
 * presets (ValueTree)
 *   PresetFile (multiple children from ReaperPresetConverter)
 *     - property: "name" (filename without extension)
 *     - property: "file" (full path)
 *     PresetBank (multiple children)
 *       - property: "name" (bank name)
 *       Preset (multiple children)
 *         - property: "name" (preset name)
 *         - property: "data" (base64 preset data)
 * ```
 */
class PresetLoader : private juce::Thread
{
public:
    /**
     * @brief Construct a new Preset Loader
     * @param apvts Reference to the AudioProcessorValueTreeState
     */
    explicit PresetLoader(juce::AudioProcessorValueTreeState& apvts);

    /**
     * @brief Destructor - ensures thread is stopped
     */
    ~PresetLoader() override;

    /**
     * @brief Request a preset refresh
     * @param jsfxPath Full path to current JSFX file (empty if none loaded)
     *
     * Triggers background loading. Safe to call from any thread.
     * If a load is already in progress, it will be cancelled and restarted.
     */
    void requestRefresh(const juce::String& jsfxPath);

    /**
     * @brief Check if a load operation is currently in progress
     * @return true if loading, false otherwise
     */
    bool isLoading() const
    {
        return isCurrentlyLoading.load();
    }

    /**
     * @brief Get the number of preset files currently loaded
     * @return Number of preset files in APVTS state
     *
     * Thread-safe. Reads from APVTS state.
     */
    int getLoadedFileCount() const;

    /**
     * @brief Get the total number of banks across all preset files
     * @return Total bank count
     *
     * Thread-safe. Reads from APVTS state.
     */
    int getLoadedBankCount() const;

private:
    // Thread entry point
    void run() override;

    /**
     * @brief Load presets for the pending JSFX path (runs on background thread)
     */
    void loadPresetsInBackground();

    /**
     * @brief Find all matching .rpl files from default locations
     * @param jsfxFile The current JSFX file
     * @param jsfxName The JSFX filename without extension
     * @return Array of preset files to load
     */
    juce::Array<juce::File> findPresetFiles(const juce::File& jsfxFile, const juce::String& jsfxName);

    /**
     * @brief Update APVTS state with loaded presets (runs on message thread)
     * @param newPresetsTree The new preset tree to store
     */
    void updatePresetsInState(juce::ValueTree newPresetsTree);

    // Reference to APVTS for reading config and storing results
    juce::AudioProcessorValueTreeState& apvts;

    // Converter for parsing .rpl files
    std::unique_ptr<ReaperPresetConverter> converter;

    // Pending JSFX path for next load operation
    juce::String pendingJsfxPath;
    juce::CriticalSection pathLock;

    // Flag indicating if a new request has been made
    std::atomic<bool> refreshRequested{false};

    // Flag indicating if loading is in progress
    std::atomic<bool> isCurrentlyLoading{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetLoader)
};
