#include "PluginProcessor.h"

#include "JsfxHelper.h"
#include "JsfxLogger.h"
#include "ParameterUtils.h"
#include "PluginEditor.h"

// Minimal slider automation callback used by JSFX UI when user tweaks sliders
static void JsfxSliderAutomateThunk(void* ctx, int parmidx, bool done)
{
    juce::ignoreUnused(done);
    auto* self = static_cast<AudioPluginAudioProcessor*>(ctx);
    if (!self)
        return;
    // Optional: notify host/parameter system about user gesture
    // For now keep it minimal; parameter syncing is handled elsewhere.
    juce::ignoreUnused(parmidx);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < PluginConstants::MaxParameters; ++i)
    {
        auto paramID = juce::String("param") + juce::String(i);
        auto paramName = juce::String("Parameter ") + juce::String(i);

        layout.add(std::make_unique<juce::AudioParameterFloat>(paramID, paramName, 0.0f, 1.0f, 0.0f));
    }

    return layout;
}

AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor(
          BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#if !JucePlugin_IsSynth
              .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
#endif
#endif
      )
    , apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    JsfxLogger::logInstanceLifecycle("AudioProcessor constructor started");

    // JsfxHelper constructor automatically initializes per-instance JSFX system

    // Set slider class name for JSFX controls
    extern const char* g_config_slider_classname;
    g_config_slider_classname = "jsfx_slider";

    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    appDataDir = appDataDir.getChildFile(JucePlugin_Name);
    if (!appDataDir.exists())
    {
        appDataDir.getChildFile("Data").createDirectory();
        appDataDir.getChildFile("Effects").createDirectory();
    }

    jsfxRootDir = appDataDir.getFullPathName();

    // Note: Global properties for directory management now handled by PersistentFileChooser

    // Populate parameter cache
    for (int i = 0; i < PluginConstants::MaxParameters; ++i)
    {
        auto paramID = juce::String("param") + juce::String(i);
        parameterCache[i] = apvts.getParameter(paramID);
    }

    // Initialize per-instance parameter value caches
    lastParameterValues.fill(-999.0f);
    lastActualParameterValues.fill(-999999.0);

    // Start timer for latency updates and parameter sync (30 Hz = ~33ms)
    startTimer(33);

    JsfxLogger::logInstanceLifecycle("AudioProcessor constructor completed");
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    JsfxLogger::logInstanceLifecycle("AudioProcessor destructor started");

    // Stop timer first to prevent any callbacks during destruction
    stopTimer();

    // Ensure all JSFX resources are cleaned up
    unloadJSFX();

    // Arrays don't need explicit clearing - they're automatically cleaned up

    JsfxLogger::logInstanceLifecycle("AudioProcessor destructor completed");
}

//==============================================================================
void AudioPluginAudioProcessor::updateRoutingConfig(const RoutingConfig& newConfig)
{
    // This is called from the message thread (UI)
    // Use triple-buffer pattern for lock-free update

    // Get the write buffer index
    int writeIdx = writeIndex.load(std::memory_order_acquire);

    // Write to the write buffer
    routingConfigs[writeIdx] = newConfig;

    // Swap write and spare buffer indices
    // The spare buffer is always the one not being read or written
    int readIdx = readIndex.load(std::memory_order_acquire);
    int spareIdx = 3 - readIdx - writeIdx; // 0+1+2=3, so spare = 3-read-write

    // Update write index to point to spare (atomic swap)
    writeIndex.store(spareIdx, std::memory_order_release);

    // Update read index to point to the buffer we just wrote (atomic swap)
    readIndex.store(writeIdx, std::memory_order_release);
}

void AudioPluginAudioProcessor::timerCallback()
{
    // Check if latency has changed and update the host
    int latency = currentJSFXLatency.load(std::memory_order_relaxed);
    if (latency != getLatencySamples())
        setLatencySamples(latency);

    // Push any queued APVTS updates from JSFX parameter changes
    // This is safe to do from timer thread (message thread)
    parameterSync.pushAPVTSUpdatesFromTimer();
}

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioPluginAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool AudioPluginAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool AudioPluginAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioPluginAudioProcessor::getNumPrograms()
{
    return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
              // so this should be at least 1, even if you're not really implementing programs.
}

int AudioPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AudioPluginAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String AudioPluginAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void AudioPluginAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    lastSampleRate = sampleRate;
    tempBuffer.setSize(1, samplesPerBlock * getTotalNumInputChannels());

    // Prepare delay line for bypass (max 10 seconds of latency should be more than enough)
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumInputChannels();
    bypassDelayLine.prepare(spec);
    bypassDelayLine.setMaximumDelayInSamples(static_cast<int>(sampleRate * 10.0));

    // Update parameter sync manager with new sample rate
    parameterSync.setSampleRate(sampleRate);

    if (sxInstance)
        JesusonicAPI.sx_extended(sxInstance, JSFX_EXT_SET_SRATE, (void*)(intptr_t)sampleRate, nullptr);
}

void AudioPluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    // Get channel counts
    const auto& mainInput = layouts.getMainInputChannelSet();
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    // Check main input/output don't exceed our maximum
    if (mainInput.size() > PluginConstants::MaxChannels || mainOutput.size() > PluginConstants::MaxChannels)
        return false;

    // Input and output layouts must match
#if !JucePlugin_IsSynth
    if (mainInput != mainOutput)
        return false;
#endif

    // Check sidechain bus if present
    if (layouts.getNumChannels(true, 1) > 0) // true = input bus, 1 = sidechain bus index
    {
        const auto& sidechainInput = layouts.getChannelSet(true, 1);

        // Sidechain must not exceed our maximum
        if (sidechainInput.size() > PluginConstants::MaxChannels)
            return false;

        // Sidechain layout must match main input layout
        if (sidechainInput != mainInput)
            return false;

        // Total channels (main + sidechain) must not exceed JSFX maximum
        if (mainInput.size() + sidechainInput.size() > PluginConstants::JsfxMaxChannels)
            return false;
    }

    return true;
#endif
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);

    juce::ScopedNoDenormals noDenormals;

    if (!sxInstance)
    {
        buffer.clear();
        return;
    }

    int numSamples = buffer.getNumSamples();
    int mainChannels = buffer.getNumChannels();

    // Get sidechain buffer if available
    auto sidechainBuffer = getBusBuffer(buffer, true, 1); // true = input, 1 = sidechain bus
    int sidechainChannels = sidechainBuffer.getNumChannels();

    // Total channels to send to JSFX (main + sidechain, capped at JSFX max)
    int totalJsfxChannels = juce::jmin(mainChannels + sidechainChannels, PluginConstants::JsfxMaxChannels);

    // Allocate temp buffer for interleaved audio: [main inputs][sidechain inputs]
    tempBuffer.setSize(1, numSamples * totalJsfxChannels, false, false, true);
    auto* tempPtr = tempBuffer.getWritePointer(0);

    // Get current routing configuration (lock-free read)
    int routingIdx = readIndex.load(std::memory_order_acquire);
    const auto& routing = routingConfigs[routingIdx];

    // Clear temp buffer first
    std::fill(tempPtr, tempPtr + numSamples * totalJsfxChannels, 0.0);

    // Apply INPUT routing matrix: JUCE inputs -> JSFX channels
    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int juceIn = 0; juceIn < mainChannels && juceIn < routing.numJuceInputs; ++juceIn)
        {
            float inputSample = buffer.getReadPointer(juceIn)[sample];

            for (int jsfxCh = 0; jsfxCh < totalJsfxChannels && jsfxCh < routing.numJsfxInputs; ++jsfxCh)
                if (routing.inputRouting[juceIn][jsfxCh])
                    tempPtr[sample * totalJsfxChannels + jsfxCh] += inputSample;
        }
    }

    // Apply SIDECHAIN routing matrix: JUCE sidechain -> JSFX channels
    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int juceSc = 0; juceSc < sidechainChannels && juceSc < routing.numJuceSidechains; ++juceSc)
        {
            float scSample = sidechainBuffer.getReadPointer(juceSc)[sample];

            for (int jsfxCh = 0; jsfxCh < totalJsfxChannels && jsfxCh < routing.numJsfxSidechains; ++jsfxCh)
                if (routing.sidechainRouting[juceSc][jsfxCh])
                    tempPtr[sample * totalJsfxChannels + jsfxCh] += scSample;
        }
    }

    // Two-way parameter synchronization between APVTS and JSFX
    // This handles:
    // - APVTS -> JSFX (user moves UI slider or host automation)
    // - JSFX -> APVTS (JSFX script changes parameter internally)
    // - Conflict resolution (APVTS takes precedence)
    parameterSync.updateFromAudioThread(sxInstance, numSamples);

    // Get transport info from host
    double tempo = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    double playState = 1.0; // 0=stopped, 1=playing, 5=recording
    double playPositionSeconds = 0.0;
    double playPositionBeats = 0.0;

    if (auto* ph = getPlayHead())
    {
        if (auto posInfo = ph->getPosition())
        {
            if (auto bpm = posInfo->getBpm())
                tempo = *bpm;

            if (auto timeSig = posInfo->getTimeSignature())
            {
                timeSigNumerator = timeSig->numerator;
                timeSigDenominator = timeSig->denominator;
            }

            if (auto ppqPos = posInfo->getPpqPosition())
                playPositionBeats = *ppqPos;

            if (auto timeInSeconds = posInfo->getTimeInSeconds())
                playPositionSeconds = *timeInSeconds;

            // Determine play state
            playState = 0.0; // stopped
            if (posInfo->getIsPlaying())
                playState = 1.0; // playing
            if (posInfo->getIsRecording())
                playState = 5.0; // recording (1 | 4)
        }
    }

    JesusonicAPI.sx_processSamples(
        sxInstance,
        tempBuffer.getWritePointer(0),
        buffer.getNumSamples(),
        totalJsfxChannels, // Use total JSFX channels including sidechain
        getSampleRate(),
        tempo,
        timeSigNumerator,
        timeSigDenominator,
        playState,
        playPositionSeconds,
        playPositionBeats,
        lastWet,
        currentWet,
        0
    );

    // Update lastWet for next block
    lastWet = currentWet;

    // Update latency atomically for the timer to read (some JSFX can have dynamic latency)
    currentJSFXLatency.store(JesusonicAPI.sx_getCurrentLatency(sxInstance), std::memory_order_relaxed);

    // Apply OUTPUT routing matrix: JSFX channels -> JUCE outputs
    // Clear output buffer first
    buffer.clear();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int jsfxOut = 0; jsfxOut < totalJsfxChannels && jsfxOut < routing.numJsfxOutputs; ++jsfxOut)
        {
            float jsfxSample = tempPtr[sample * totalJsfxChannels + jsfxOut];

            for (int juceOut = 0; juceOut < mainChannels && juceOut < routing.numJuceOutputs; ++juceOut)
                if (routing.outputRouting[jsfxOut][juceOut])
                    buffer.getWritePointer(juceOut)[sample] += jsfxSample;
        }
    }
}

void AudioPluginAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);

    // Introduce the same latency as the JSFX plugin to maintain timing alignment
    int latencySamples = getLatencySamples();
    if (latencySamples > 0)
    {
        bypassDelayLine.setDelay(static_cast<float>(latencySamples));

        // Process each channel through the delay line
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* channelData = buffer.getWritePointer(channel);
            juce::dsp::AudioBlock<float> block(&channelData, 1, buffer.getNumSamples());
            juce::dsp::ProcessContextReplacing<float> context(block);
            bypassDelayLine.process(context);
        }
    }
    // If no latency, just pass the audio through unchanged
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor(*this);
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
    {
        if (xmlState->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));

            // Restore wet amount
            currentWet = apvts.state.getProperty("wetAmount", 1.0);
            lastWet = currentWet;

            auto jsfxPath = getCurrentJSFXPath();
            if (jsfxPath.isNotEmpty())
            {
                juce::File jsfxFile(jsfxPath);
                if (jsfxFile.existsAsFile())
                {
                    loadJSFX(jsfxFile);

                    // Restore routing configuration after JSFX loads
                    auto routingStr = apvts.state.getProperty("ioMatrixRouting", "").toString();
                    if (routingStr.isNotEmpty())
                        restoreRoutingFromString(routingStr);
                }
            }
        }
    }
}

void AudioPluginAudioProcessor::restoreRoutingFromString(const juce::String& routingStr)
{
    auto parts = juce::StringArray::fromTokens(routingStr, ",", "");
    if (parts.size() != 3)
        return; // Invalid format

    // Get current routing config to read channel counts
    int currentRead = readIndex.load(std::memory_order_acquire);
    const auto& currentConfig = routingConfigs[currentRead];

    RoutingConfig newConfig;
    newConfig.numJuceInputs = currentConfig.numJuceInputs;
    newConfig.numJuceSidechains = currentConfig.numJuceSidechains;
    newConfig.numJuceOutputs = currentConfig.numJuceOutputs;
    newConfig.numJsfxInputs = currentConfig.numJsfxInputs;
    newConfig.numJsfxSidechains = currentConfig.numJsfxSidechains;
    newConfig.numJsfxOutputs = currentConfig.numJsfxOutputs;

    // Clear all routing first
    for (auto& row : newConfig.inputRouting)
        row.fill(false);
    for (auto& row : newConfig.sidechainRouting)
        row.fill(false);
    for (auto& row : newConfig.outputRouting)
        row.fill(false);

    // Decode input routing: [JUCE input][JSFX input]
    if (parts[0].isNotEmpty())
    {
        int idx = 0;
        for (int juceIn = 0; juceIn < newConfig.numJuceInputs; ++juceIn)
        {
            for (int jsfxIn = 0; jsfxIn < newConfig.numJsfxInputs; ++jsfxIn)
            {
                if (idx < parts[0].length())
                    newConfig.inputRouting[juceIn][jsfxIn] = (parts[0][idx] == '1');
                idx++;
            }
        }
    }

    // Decode sidechain routing: [JUCE sidechain][JSFX sidechain]
    if (parts[1].isNotEmpty())
    {
        int idx = 0;
        for (int juceSc = 0; juceSc < newConfig.numJuceSidechains; ++juceSc)
        {
            for (int jsfxSc = 0; jsfxSc < newConfig.numJsfxSidechains; ++jsfxSc)
            {
                if (idx < parts[1].length())
                    newConfig.sidechainRouting[juceSc][jsfxSc] = (parts[1][idx] == '1');
                idx++;
            }
        }
    }

    // Decode output routing: [JSFX output][JUCE output]
    if (parts[2].isNotEmpty())
    {
        int idx = 0;
        for (int jsfxOut = 0; jsfxOut < newConfig.numJsfxOutputs; ++jsfxOut)
        {
            for (int juceOut = 0; juceOut < newConfig.numJuceOutputs; ++juceOut)
            {
                if (idx < parts[2].length())
                    newConfig.outputRouting[jsfxOut][juceOut] = (parts[2][idx] == '1');
                idx++;
            }
        }
    }

    // Apply the restored routing config
    updateRoutingConfig(newConfig);
}

bool AudioPluginAudioProcessor::loadJSFX(const juce::File& jsfxFile)
{
    if (!jsfxFile.existsAsFile())
        return false;

    unloadJSFX();

    // Use the JSFX file's location directly - no copying needed!
    // This allows:
    // - Live updates to source files
    // - No disk space duplication
    // - Proper dependency resolution from source directory (*.jsfx-inc, *.png, *.rpl)
    juce::File sourceDir = jsfxFile.getParentDirectory();
    juce::String fileName = jsfxFile.getFileName();

    // sx_createInstance expects (dir_root, relative_path_to_file)
    // With our JSFX patch, we pass the parent directory as dir_root and just the filename
    // This sets m_effectdir = sourceDir, allowing relative imports to work
    bool wantWak = false;
    sxInstance =
        JesusonicAPI.sx_createInstance(sourceDir.getFullPathName().toRawUTF8(), fileName.toRawUTF8(), &wantWak);

    if (!sxInstance)
        return false;

    apvts.state.setProperty(jsfxPathParamID, jsfxFile.getFullPathName(), nullptr);

    // Provide host context and slider automate callback for UI
    // Use 'this' as host context so we can route callbacks if needed
    sx_set_host_ctx(sxInstance, this, JsfxSliderAutomateThunk);

    currentJSFXName = jsfxFile.getFileNameWithoutExtension();

    JesusonicAPI.sx_extended(sxInstance, JSFX_EXT_SET_SRATE, (void*)(intptr_t)lastSampleRate, nullptr);

    updateParameterMapping();

    // Get and set initial latency
    int latencySamples = JesusonicAPI.sx_getCurrentLatency(sxInstance);
    currentJSFXLatency.store(latencySamples, std::memory_order_relaxed);
    setLatencySamples(latencySamples);

    // Initialize routing configuration with current bus layout
    auto bus = getBusesLayout();
    for (int i = 0; i < 3; ++i)
    {
        routingConfigs[i].numJuceInputs = bus.getMainInputChannels();
        routingConfigs[i].numJuceSidechains = getBus(true, 1) ? bus.getNumChannels(true, 1) : 0;
        routingConfigs[i].numJuceOutputs = bus.getMainOutputChannels();
        routingConfigs[i].numJsfxInputs = routingConfigs[i].numJuceInputs;
        routingConfigs[i].numJsfxSidechains = routingConfigs[i].numJuceSidechains;
        routingConfigs[i].numJsfxOutputs = routingConfigs[i].numJuceOutputs;
        routingConfigs[i].setDiagonal();
    }

    // Note: Directory remembering now handled by PersistentFileChooser in editor

    return true;
}

void AudioPluginAudioProcessor::unloadJSFX()
{
    if (sxInstance)
    {
        // Destroy JSFX instance and ensure proper cleanup
        JesusonicAPI.sx_destroyInstance(sxInstance);
        sxInstance = nullptr;

        // Reset latency to 0 when unloading
        currentJSFXLatency.store(0, std::memory_order_relaxed);
        setLatencySamples(0);

        // Reset parameter value caches to initial state
        std::fill(lastParameterValues.begin(), lastParameterValues.end(), -999.0f);
        std::fill(lastActualParameterValues.begin(), lastActualParameterValues.end(), -999999.0);

        // Reset parameter sync manager
        parameterSync.reset();
    }

    apvts.state.setProperty(jsfxPathParamID, "", nullptr);

    currentJSFXName.clear();
    numActiveParams = 0;
}

juce::String AudioPluginAudioProcessor::getCurrentJSFXPath() const
{
    return apvts.state.getProperty(jsfxPathParamID, "").toString();
}

// Directory management methods removed - now handled by PersistentFileChooser utility

juce::String AudioPluginAudioProcessor::getJSFXParameterName(int index) const
{
    if (!ParameterUtils::isValidParameterIndex(sxInstance, index, numActiveParams))
        return "Parameter " + juce::String(index);

    return ParameterUtils::getParameterName(sxInstance, index);
}

bool AudioPluginAudioProcessor::getJSFXParameterRange(int index, double& minVal, double& maxVal, double& step) const
{
    if (!ParameterUtils::isValidParameterIndex(sxInstance, index, numActiveParams))
        return false;

    return ParameterUtils::getParameterRange(sxInstance, index, minVal, maxVal, step);
}

bool AudioPluginAudioProcessor::isJSFXParameterEnum(int index) const
{
    if (!ParameterUtils::isValidParameterIndex(sxInstance, index, numActiveParams))
        return false;

    return ParameterUtils::detectParameterType(sxInstance, index) == ParameterUtils::ParameterType::Enum;
}

juce::String AudioPluginAudioProcessor::getJSFXParameterDisplayText(int index, double value) const
{
    if (!ParameterUtils::isValidParameterIndex(sxInstance, index, numActiveParams))
        return juce::String(value);

    return ParameterUtils::getParameterDisplayText(sxInstance, index, value);
}

void AudioPluginAudioProcessor::updateParameterMapping()
{
    if (!sxInstance)
    {
        numActiveParams = 0;
        return;
    }

    numActiveParams = JesusonicAPI.sx_getNumParms(sxInstance);
    numActiveParams = juce::jmin(numActiveParams, PluginConstants::MaxParameters);

    for (int i = 0; i < numActiveParams; ++i)
    {
        double currentVal = JesusonicAPI.sx_getParmVal(
            sxInstance,
            i,
            &parameterRanges[i].minVal,
            &parameterRanges[i].maxVal,
            &parameterRanges[i].step
        );

        JsfxLogger::debug(
            "ParameterMapping",
            "Param "
                + juce::String(i)
                + " from JSFX: currentVal="
                + juce::String(currentVal, 3)
                + " range=["
                + juce::String(parameterRanges[i].minVal, 3)
                + ".."
                + juce::String(parameterRanges[i].maxVal, 3)
                + "]"
                + " step="
                + juce::String(parameterRanges[i].step, 3)
        );

        if (auto* param = parameterCache[i])
        {
            float normalizedValue = ParameterUtils::actualToNormalizedValue(sxInstance, i, currentVal);
            param->setValueNotifyingHost(normalizedValue);
        }
    }

    // Initialize the parameter sync manager with current state
    parameterSync.initialize(parameterCache, sxInstance, numActiveParams, lastSampleRate);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
