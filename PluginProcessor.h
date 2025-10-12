#pragma once

//
#include <jsfx_api.h>
#include <juce_audio_utils/juce_audio_utils.h>

extern jsfxAPI JesusonicAPI;
extern HINSTANCE g_hInst;

//==============================================================================
class AudioPluginAudioProcessor final : public juce::AudioProcessor
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

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateParameterMapping();

    static constexpr int maxParameters = 256;
    static constexpr const char* jsfxPathParamID = "jsfxFilePath";

    struct ParameterRange
    {
        double minVal = 0.0;
        double maxVal = 1.0;
        double step = 0.0;
    };

    juce::AudioProcessorValueTreeState apvts;
    std::vector<juce::RangedAudioParameter*> parameterCache;
    std::vector<ParameterRange> parameterRanges;

    SX_Instance* sxInstance = nullptr;
    juce::AudioBuffer<double> tempBuffer;

    juce::String currentJSFXName;
    juce::String jsfxRootDir;
    int numActiveParams = 0;
    double lastSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};
