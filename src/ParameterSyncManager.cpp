#include "ParameterSyncManager.h"

#include "JsfxLogger.h"

#include <cmath>

ParameterSyncManager::ParameterSyncManager()
{
}

ParameterSyncManager::~ParameterSyncManager()
{
}

void ParameterSyncManager::initialize(
    const std::array<juce::RangedAudioParameter*, PluginConstants::MaxParameters>& apvtsParamsIn,
    SX_Instance* jsfxInstance,
    int numParamsIn,
    double sampleRate
)
{
    apvtsParams = apvtsParamsIn;
    numParams = numParamsIn;
    currentSampleRate = sampleRate;

    // Initialize sync state from current values
    for (int i = 0; i < numParams; ++i)
    {
        if (i < static_cast<int>(apvtsParams.size()) && apvtsParams[i] && jsfxInstance)
        {
            // Get initial values from both sides
            float apvtsValue = apvtsParams[i]->getValue();

            double minVal, maxVal, step;
            double jsfxValue = JesusonicAPI.sx_getParmVal(jsfxInstance, i, &minVal, &maxVal, &step);

            // Store initial state (release ensures writes are visible to other threads)
            parameterStates[i].apvtsValue.store(apvtsValue, std::memory_order_release);
            parameterStates[i].jsfxValue.store(jsfxValue, std::memory_order_release);
            parameterStates[i].apvtsNeedsUpdate.store(false, std::memory_order_release);

            // Initialize smoother with current value
            parameterStates[i].smoothedJsfxValue.reset(sampleRate, PluginConstants::ParameterSmoothingMs / 1000.0);
            parameterStates[i].smoothedJsfxValue.setCurrentAndTargetValue(jsfxValue);

            JsfxLogger::debug(
                "ParameterSync",
                "Initialized param "
                    + juce::String(i)
                    + " - APVTS: "
                    + juce::String(apvtsValue, 3)
                    + " JSFX: "
                    + juce::String(jsfxValue, 3)
            );
        }
    }

    JsfxLogger::info("ParameterSync", "Initialized with " + juce::String(numParams) + " parameters");
}

void ParameterSyncManager::updateFromAudioThread(SX_Instance* jsfxInstance, int numSamples)
{
    if (!jsfxInstance || numParams == 0)
        return;

    // Check each parameter for changes
    for (int i = 0; i < numParams; ++i)
    {
        if (i >= static_cast<int>(apvtsParams.size()) || !apvtsParams[i])
            continue;

        auto& state = parameterStates[i];

        // Read current values
        float currentApvtsValue = apvtsParams[i]->getValue();

        double minVal, maxVal, step;
        double currentJsfxValue = JesusonicAPI.sx_getParmVal(jsfxInstance, i, &minVal, &maxVal, &step);

        // Load stored values atomically (acquire ensures we see writes from timer thread)
        float storedApvtsValue = state.apvtsValue.load(std::memory_order_acquire);
        double storedJsfxValue = state.jsfxValue.load(std::memory_order_acquire);

        // Check if APVTS changed (user moved UI control or host automation)
        bool apvtsChanged = std::abs(currentApvtsValue - storedApvtsValue) > 0.0001f;

        // Check if JSFX changed (JSFX script modified parameter internally)
        bool jsfxChanged = std::abs(currentJsfxValue - storedJsfxValue) > 0.0001;

        if (apvtsChanged && jsfxChanged)
        {
            // Both changed - APVTS takes precedence
            JsfxLogger::debug(
                "ParameterSync",
                "Param " + juce::String(i) + " - both changed, APVTS wins: " + juce::String(currentApvtsValue, 3)
            );

            // Set smoothing target to new APVTS value
            double jsfxTargetValue = normalizedToJsfx(jsfxInstance, i, currentApvtsValue);
            state.smoothedJsfxValue.setTargetValue(jsfxTargetValue);

            // Update our state atomically (release makes writes visible to timer thread)
            state.apvtsValue.store(currentApvtsValue, std::memory_order_release);
            state.jsfxValue.store(jsfxTargetValue, std::memory_order_release);
        }
        else if (apvtsChanged)
        {
            // Only APVTS changed - set new smoothing target
            double jsfxTargetValue = normalizedToJsfx(jsfxInstance, i, currentApvtsValue);
            state.smoothedJsfxValue.setTargetValue(jsfxTargetValue);

            // Update our state atomically (release makes writes visible to timer thread)
            state.apvtsValue.store(currentApvtsValue, std::memory_order_release);
            state.jsfxValue.store(jsfxTargetValue, std::memory_order_release);
        }
        else if (jsfxChanged)
        {
            // Only JSFX changed - queue update for APVTS (can't modify APVTS from audio thread)
            float normalizedValue = static_cast<float>(jsfxToNormalized(jsfxInstance, i, currentJsfxValue));

            // Update smoother to match JSFX value (skip smoothing for JSFX->APVTS direction)
            state.smoothedJsfxValue.setCurrentAndTargetValue(currentJsfxValue);

            // Queue the update atomically (release makes writes visible to timer thread)
            state.pendingApvtsValue.store(normalizedValue, std::memory_order_release);
            state.jsfxValue.store(currentJsfxValue, std::memory_order_release);
            state.apvtsNeedsUpdate.store(true, std::memory_order_release);

            JsfxLogger::debug(
                "ParameterSync",
                "Param "
                    + juce::String(i)
                    + " - JSFX changed, queueing APVTS update: "
                    + juce::String(normalizedValue, 3)
            );
        }

        // Always push smoothed value to JSFX (smoothing happens here)
        // Skip ahead by the number of samples in this block to properly advance the smoother
        state.smoothedJsfxValue.skip(numSamples - 1);
        double smoothedValue = state.smoothedJsfxValue.getNextValue();
        JesusonicAPI.sx_setParmVal(jsfxInstance, i, smoothedValue, 0);
    }
}

void ParameterSyncManager::pushAPVTSUpdatesFromTimer()
{
    // This runs on the message thread, safe to modify APVTS
    for (int i = 0; i < numParams; ++i)
    {
        if (i >= static_cast<int>(apvtsParams.size()) || !apvtsParams[i])
            continue;

        auto& state = parameterStates[i];

        // Check if update is needed (acquire ensures we see all writes from audio thread)
        if (state.apvtsNeedsUpdate.load(std::memory_order_acquire))
        {
            // Load the queued value (acquire ensures we see the value written by audio thread)
            float pendingValue = state.pendingApvtsValue.load(std::memory_order_acquire);

            // Push the queued value to APVTS
            apvtsParams[i]->setValueNotifyingHost(pendingValue);

            // Update our APVTS state tracking (release makes writes visible to audio thread)
            state.apvtsValue.store(pendingValue, std::memory_order_release);
            state.apvtsNeedsUpdate.store(false, std::memory_order_release);

            JsfxLogger::debug(
                "ParameterSync",
                "Pushed JSFX->APVTS update for param " + juce::String(i) + ": " + juce::String(pendingValue, 3)
            );
        }
    }
}

void ParameterSyncManager::reset()
{
    JsfxLogger::info("ParameterSync", "Resetting sync state");

    numParams = 0;

    // Reset all parameter states to defaults
    for (auto& state : parameterStates)
    {
        state.apvtsValue.store(-999.0f, std::memory_order_release);
        state.jsfxValue.store(-999999.0, std::memory_order_release);
        state.apvtsNeedsUpdate.store(false, std::memory_order_release);
        state.pendingApvtsValue.store(0.0f, std::memory_order_release);
        state.smoothedJsfxValue.setCurrentAndTargetValue(0.0);
    }

    // Clear parameter references
    apvtsParams.fill(nullptr);
}

void ParameterSyncManager::setSampleRate(double sampleRate)
{
    currentSampleRate = sampleRate;

    // Update all smoothers with new sample rate
    for (int i = 0; i < numParams; ++i)
    {
        auto& state = parameterStates[i];
        double currentValue = state.smoothedJsfxValue.getCurrentValue();
        state.smoothedJsfxValue.reset(sampleRate, PluginConstants::ParameterSmoothingMs / 1000.0);
        state.smoothedJsfxValue.setCurrentAndTargetValue(currentValue);
    }

    JsfxLogger::info("ParameterSync", "Updated sample rate to " + juce::String(sampleRate, 1) + " Hz");
}

double ParameterSyncManager::jsfxToNormalized(SX_Instance* instance, int paramIndex, double jsfxValue)
{
    if (!instance)
        return 0.0;

    double minVal, maxVal, step;
    JesusonicAPI.sx_getParmVal(instance, paramIndex, &minVal, &maxVal, &step);

    if (maxVal > minVal)
        return (jsfxValue - minVal) / (maxVal - minVal);

    return 0.0;
}

double ParameterSyncManager::normalizedToJsfx(SX_Instance* instance, int paramIndex, float normalizedValue)
{
    if (!instance)
        return 0.0;

    double minVal, maxVal, step;
    JesusonicAPI.sx_getParmVal(instance, paramIndex, &minVal, &maxVal, &step);

    return minVal + normalizedValue * (maxVal - minVal);
}
