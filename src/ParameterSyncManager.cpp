#include "ParameterSyncManager.h"

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

            DBG("Initialized param "
                << i
                << " - APVTS: "
                << juce::String(apvtsValue, 3)
                << " JSFX: "
                << juce::String(jsfxValue, 3));
        }
    }
}

void ParameterSyncManager::updateFromAudioThread(SX_Instance* jsfxInstance, int numSamples)
{
    if (!jsfxInstance || numParams == 0)
        return;

    // Note: Audio processing is suspended during JSFX loading/unloading,
    // so we don't need to check for parameter count mismatches here

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

            // Convert and set JSFX value directly (no smoothing)
            double jsfxTargetValue = normalizedToJsfx(jsfxInstance, i, currentApvtsValue);
            JesusonicAPI.sx_setParmVal(jsfxInstance, i, jsfxTargetValue, 0);

            // Update our state atomically (release makes writes visible to timer thread)
            state.apvtsValue.store(currentApvtsValue, std::memory_order_release);
            state.jsfxValue.store(jsfxTargetValue, std::memory_order_release);
        }
        else if (apvtsChanged)
        {
            // Only APVTS changed - set JSFX value directly (no smoothing)
            double jsfxTargetValue = normalizedToJsfx(jsfxInstance, i, currentApvtsValue);
            JesusonicAPI.sx_setParmVal(jsfxInstance, i, jsfxTargetValue, 0);

            // Update our state atomically (release makes writes visible to timer thread)
            state.apvtsValue.store(currentApvtsValue, std::memory_order_release);
            state.jsfxValue.store(jsfxTargetValue, std::memory_order_release);
        }
        else if (jsfxChanged)
        {
            // Only JSFX changed - queue update for APVTS (can't modify APVTS from audio thread)
            float normalizedValue = static_cast<float>(jsfxToNormalized(jsfxInstance, i, currentJsfxValue));

            // Queue the update atomically (release makes writes visible to timer thread)
            state.pendingApvtsValue.store(normalizedValue, std::memory_order_release);
            state.jsfxValue.store(currentJsfxValue, std::memory_order_release);
            state.apvtsNeedsUpdate.store(true, std::memory_order_release);
        }
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
        }
    }
}

void ParameterSyncManager::reset()
{
    numParams = 0;

    // Reset all parameter states to defaults
    for (auto& state : parameterStates)
    {
        state.apvtsValue.store(-999.0f, std::memory_order_release);
        state.jsfxValue.store(-999999.0, std::memory_order_release);
        state.apvtsNeedsUpdate.store(false, std::memory_order_release);
        state.pendingApvtsValue.store(0.0f, std::memory_order_release);
    }

    // Clear parameter references
    apvtsParams.fill(nullptr);
}

void ParameterSyncManager::setSampleRate(double sampleRate)
{
    currentSampleRate = sampleRate;
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
