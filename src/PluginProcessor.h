#pragma once

#include <jsfx.h>
//

#include "JsfxHelper.h"
#include "ParameterSyncManager.h"
#include "PluginConstants.h"
#include "PresetLoader.h"

#include <atomic>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_dsp/juce_dsp.h>

extern jsfxAPI JesusonicAPI;

//==============================================================================
// Lock-free routing configuration for realtime-safe communication
struct RoutingConfig
{
    // Routing matrices stored as flat arrays
    // Input: JUCE input channels -> JSFX channels (rows = JUCE, cols = JSFX)
    std::array<std::array<bool, PluginConstants::MaxChannels>, PluginConstants::MaxChannels> inputRouting{};

    // Sidechain: JUCE sidechain channels -> JSFX channels (rows = JUCE SC, cols = JSFX)
    std::array<std::array<bool, PluginConstants::MaxChannels>, PluginConstants::MaxChannels> sidechainRouting{};

    // Output: JSFX channels -> JUCE output channels (rows = JSFX, cols = JUCE)
    std::array<std::array<bool, PluginConstants::MaxChannels>, PluginConstants::MaxChannels> outputRouting{};

    int numJuceInputs = 0;
    int numJuceSidechains = 0;
    int numJuceOutputs = 0;
    int numJsfxInputs = 0;
    int numJsfxSidechains = 0;
    int numJsfxOutputs = 0;

    // Initialize with 1:1 diagonal routing
    void setDiagonal()
    {
        // Clear all
        for (auto& row : inputRouting)
            row.fill(false);
        for (auto& row : sidechainRouting)
            row.fill(false);
        for (auto& row : outputRouting)
            row.fill(false);

        // Set diagonals
        for (int i = 0; i < juce::jmin(numJuceInputs, numJsfxInputs); ++i)
            inputRouting[i][i] = true;
        for (int i = 0; i < juce::jmin(numJuceSidechains, numJsfxSidechains); ++i)
            sidechainRouting[i][i] = true;
        for (int i = 0; i < juce::jmin(numJsfxOutputs, numJuceOutputs); ++i)
            outputRouting[i][i] = true;
    }
};

//==============================================================================
class AudioPluginAudioProcessor final
    : public juce::AudioProcessor
    , public JsfxHelper
    , private juce::Timer
{
public:
    //==============================================================================
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    auto* getSXInstancePtr() noexcept
    {
        return sxInstance;
    }

    bool loadJSFX(const juce::File& jsfxFile);
    void unloadJSFX();

    juce::String getCurrentJSFXPath() const;

    juce::String getCurrentJSFXName() const
    {
        return currentJSFXName;
    }

    juce::String getCurrentJSFXAuthor() const
    {
        return currentJSFXAuthor;
    }

    // Note: Directory management moved to PersistentFileChooser utility

    int getNumActiveParameters() const
    {
        return numActiveParams;
    }

    juce::String getJSFXParameterName(int index) const;
    bool getJSFXParameterRange(int index, double& minVal, double& maxVal, double& step) const;
    bool isJSFXParameterEnum(int index) const;
    juce::String getJSFXParameterDisplayText(int index, double value) const;

    juce::AudioProcessorValueTreeState& getAPVTS()
    {
        return apvts;
    }

    void setWetAmount(double wet)
    {
        currentWet = juce::jlimit(0.0, 1.0, wet);
        apvts.state.setProperty("wetAmount", currentWet, nullptr);
    }

    double getWetAmount() const
    {
        return currentWet;
    }

    // Update routing configuration from UI (called from message thread)
    void updateRoutingConfig(const RoutingConfig& newConfig);

    // Preset loading (works with or without editor)
    // Call this from:
    // - Editor UI when user selects preset from LibraryBrowser
    // - processBlock() when handling MIDI Program Change messages
    // - Host automation/preset recall
    // - Any other preset loading scenario
    bool loadPresetFromBase64(const juce::String& base64Data);

private:
    // Helper to restore routing from encoded string
    void restoreRoutingFromString(const juce::String& routingStr);
    //==============================================================================
    void timerCallback() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateParameterMapping();

    static constexpr const char* jsfxPathParamID = "jsfxFilePath";

    struct ParameterRange
    {
        double minVal = 0.0;
        double maxVal = 1.0;
        double step = 0.0;
    };

    juce::AudioProcessorValueTreeState apvts;
    std::array<juce::RangedAudioParameter*, PluginConstants::MaxParameters> parameterCache;
    std::array<ParameterRange, PluginConstants::MaxParameters> parameterRanges;

    SX_Instance* sxInstance = nullptr;
    juce::AudioBuffer<double> tempBuffer;

    juce::String currentJSFXName;
    juce::String currentJSFXAuthor;
    juce::String jsfxRootDir;
    int numActiveParams = 0;
    double lastSampleRate = 44100.0;

    double lastWet = 1.0;
    double currentWet = 1.0;

    std::atomic<int> currentJSFXLatency{0};
    juce::dsp::DelayLine<float> bypassDelayLine;

    // Two-way parameter synchronization between APVTS and JSFX
    ParameterSyncManager parameterSync;

    // Async preset loader
    std::unique_ptr<PresetLoader> presetLoader;

    // Lock-free routing configuration (triple buffer pattern)
    RoutingConfig routingConfigs[3]; // Triple buffer for lock-free updates
    std::atomic<int> readIndex{0};   // Index used by audio thread (processBlock)
    std::atomic<int> writeIndex{1};  // Index used by UI thread for writing
    // Third buffer (index 2) is the swap buffer

    // MIDI support
    static double midiSendRecvCallback(void* ctx, int action, double* ts, double* msg1, double* msg23, double* midibus);
    juce::MidiBuffer* currentMidiInputBuffer = nullptr;            // Set during processBlock
    std::unique_ptr<juce::MidiBuffer::Iterator> midiInputIterator; // Iterator for reading MIDI sequentially
    juce::MidiBuffer currentMidiOutputBuffer;                      // Accumulated during processBlock
    std::vector<unsigned char> midiTempBuffer;                     // Temp storage for MIDI messages

    // Note: Global properties management moved to PersistentFileChooser utility

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};
