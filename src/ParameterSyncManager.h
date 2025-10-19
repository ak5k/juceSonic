#pragma once

#include <Config.h>

#include <array>
#include <atomic>
#include <jsfx.h>
#include <juce_audio_processors/juce_audio_processors.h>

extern jsfxAPI JesusonicAPI;

/**
 * Two-way parameter synchronization mechanism between JUCE APVTS and JSFX.
 *
 * Thread Safety:
 * - processBlock() calls are made from audio thread (reads both, writes to temp state)
 * - Timer calls are made from message thread (writes to APVTS from temp state)
 * - APVTS always takes precedence when both sides change simultaneously
 */
class ParameterSyncManager
{
public:
    ParameterSyncManager();
    ~ParameterSyncManager();

    /**
     * Initialize the sync manager with parameter references
     * @param apvtsParams Array of APVTS parameter pointers
     * @param jsfxInstance JSFX instance to sync with
     * @param numParams Number of active parameters
     * @param sampleRate Current sample rate for smoothing
     */
    void initialize(
        const std::array<juce::RangedAudioParameter*, PluginConstants::MaxParameters>& apvtsParams,
        SX_Instance* jsfxInstance,
        int numParams,
        double sampleRate
    );

    /**
     * Update sync state from audio thread (processBlock).
     * Detects changes and queues updates for both APVTS and JSFX.
     * @param jsfxInstance Current JSFX instance
     * @param numSamples Number of samples in the current block
     */
    void updateFromAudioThread(SX_Instance* jsfxInstance, int numSamples);

    /**
     * Push queued APVTS updates from timer thread (message thread).
     * This is the only place where APVTS parameters are modified.
     */
    void pushAPVTSUpdatesFromTimer();

    /**
     * Reset all sync state (call when loading new JSFX)
     */
    void reset();

    /**
     * Update sample rate for smoothing (call when sample rate changes)
     * @param sampleRate New sample rate
     */
    void setSampleRate(double sampleRate);

private:
    struct ParameterState
    {
        // Last known values from each side (accessed from both threads)
        std::atomic<float> apvtsValue{-999.0f};   // Normalized APVTS value (0.0-1.0)
        std::atomic<double> jsfxValue{-999999.0}; // Actual JSFX value (in JSFX range)

        // Pending updates to push from timer thread
        std::atomic<bool> apvtsNeedsUpdate{false};
        std::atomic<float> pendingApvtsValue{0.0f};
    };

    // Sync state for each parameter
    std::array<ParameterState, PluginConstants::MaxParameters> parameterStates;

    // References to APVTS parameters (for timer thread updates)
    std::array<juce::RangedAudioParameter*, PluginConstants::MaxParameters> apvtsParams;

    // Number of active parameters
    int numParams = 0;

    // Current sample rate
    double currentSampleRate = 44100.0;

    // Helper to convert between JSFX and normalized values
    static double jsfxToNormalized(SX_Instance* instance, int paramIndex, double jsfxValue);
    static double normalizedToJsfx(SX_Instance* instance, int paramIndex, float normalizedValue);
};
