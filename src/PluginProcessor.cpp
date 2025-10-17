#include "PluginProcessor.h"

#include "JsfxHelper.h"
#include "JsfxLogger.h"
#include "ParameterUtils.h"
#include "PluginEditor.h"

#include <algorithm>

// Forward declaration for JSFX MIDI function (not in jsfxAPI struct but exists in effectproc.cpp)
extern void sx_set_midi_ctx(
    SX_Instance* sx,
    double (*midi_sendrecv)(void* ctx, int action, double* ts, double* msg1, double* msg23, double* midibus),
    void* midi_ctxdata
);

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
#if !JucePlugin_IsSynth
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#if !JucePlugin_IsSynth && !JucePlugin_IsMidiEffect
              .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
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
    // Clean up previous audio state
    bypassDelayLine.reset();
    tempBuffer.clear();

    // Initialize audio state for new configuration
    lastSampleRate = sampleRate;
    tempBuffer.setSize(1, samplesPerBlock * getTotalNumInputChannels());

    // Prepare delay line for bypass (max 10 seconds of latency should be more than enough)
    // Only prepare if we have audio channels (MIDI-only effects won't have channels)
    int numChannels = getTotalNumInputChannels();
    if (numChannels > 0)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
        spec.numChannels = static_cast<uint32_t>(numChannels);
        bypassDelayLine.prepare(spec);
        bypassDelayLine.setMaximumDelayInSamples(static_cast<int>(sampleRate * 10.0));
    }

    // Update parameter sync manager with new sample rate
    parameterSync.setSampleRate(sampleRate);

    if (sxInstance)
        JesusonicAPI.sx_extended(sxInstance, JSFX_EXT_SET_SRATE, (void*)(intptr_t)sampleRate, nullptr);

    // Initialize routing matrix with valid bus configuration
    // This must be done here (not in loadJSFX) because bus layout isn't ready during construction
    if (sxInstance)
    {
        auto bus = getBusesLayout();
        int juceOutputs = bus.getMainOutputChannels();

        DBG("=== ROUTING INITIALIZATION IN PREPARE ===");
        DBG("JUCE bus layout: " << bus.getMainInputChannels() << " inputs, " << juceOutputs << " outputs");

        // Initialize all routing configs with diagonal routing
        for (int i = 0; i < 3; ++i)
        {
            routingConfigs[i].numJuceOutputs = juceOutputs;
            routingConfigs[i].numJsfxOutputs = juceOutputs;
            routingConfigs[i].setDiagonal();
        }

        DBG("Routing matrix initialized: "
            << routingConfigs[0].numJsfxOutputs
            << " JSFX outputs -> "
            << routingConfigs[0].numJuceOutputs
            << " JUCE outputs");
    }
}

void AudioPluginAudioProcessor::releaseResources()
{
    // Note: Don't clean up here - releaseResources is not guaranteed to be called!
    // All cleanup happens at the start of prepareToPlay() instead.
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Get channel sets
    const auto& mainInput = layouts.getMainInputChannelSet();
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    // Require at least one audio channel on main input and main output
    if (mainInput.size() == 0 || mainOutput.size() == 0)
        return false;

    // Check main input/output don't exceed our maximum
    if (mainInput.size() > PluginConstants::MaxChannels || mainOutput.size() > PluginConstants::MaxChannels)
        return false;

    // Require main input and main output layouts to match exactly
    if (mainInput != mainOutput)
        return false;

    // If a sidechain is present, it must match the main input layout exactly
    if (layouts.getNumChannels(true, 1) > 0)
    {
        const auto& sidechainInput = layouts.getChannelSet(true, 1);

        // Sidechain must not exceed our maximum
        if (sidechainInput.size() > PluginConstants::MaxChannels)
            return false;

        // Sidechain must match main input
        if (sidechainInput != mainInput)
            return false;

        // Total channels (main + sidechain) must not exceed JSFX maximum
        if (mainInput.size() + sidechainInput.size() > PluginConstants::JsfxMaxChannels)
            return false;
    }

    return true;
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    if (!sxInstance)
    {
        buffer.clear();
        midiMessages.clear();
        return;
    }

    // Setup MIDI routing: input from host, output accumulator
    currentMidiInputBuffer = &midiMessages;
    midiInputIterator = std::make_unique<juce::MidiBuffer::Iterator>(midiMessages); // Initialize iterator at start
    currentMidiOutputBuffer.clear();

    int numSamples = buffer.getNumSamples();
    int mainChannels = buffer.getNumChannels();

    // Get sidechain buffer if available (bus index 1)
    // Check if sidechain bus exists first to avoid assertion
    bool hasSidechainBus = (getBusCount(true) > 1);
    auto sidechainBuffer = hasSidechainBus ? getBusBuffer(buffer, true, 1) : juce::AudioBuffer<float>();
    int sidechainChannels = sidechainBuffer.getNumChannels();

    // Total channels to send to JSFX (main + sidechain, capped at JSFX max)
    // For MIDI instruments with 0 inputs, use the output channel count instead
    int inputChannelCount = mainChannels + sidechainChannels;
    if (inputChannelCount == 0 && getBusCount(false) > 0)
    {
        // MIDI instrument - use output bus channel count
        inputChannelCount = getBus(false, 0)->getNumberOfChannels();
    }
    int totalJsfxChannels = juce::jmin(inputChannelCount, PluginConstants::JsfxMaxChannels);

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

    // Transfer MIDI output from JSFX back to host
    midiMessages.clear();
    midiMessages.addEvents(currentMidiOutputBuffer, 0, numSamples, 0);

    // Clear MIDI input pointer to prevent dangling reference
    currentMidiInputBuffer = nullptr;
}

void AudioPluginAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);

    // Introduce the same latency as the JSFX plugin to maintain timing alignment
    // Only apply delay if we have audio channels and latency is configured
    int latencySamples = getLatencySamples();
    if (latencySamples > 0 && buffer.getNumChannels() > 0)
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
    // If no latency or no audio channels, just pass through unchanged (MIDI will pass through automatically)
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
            // Step 1: Restore the state tree (this restores parameter values to APVTS)
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));

            // Step 2: Restore wet amount
            currentWet = apvts.state.getProperty("wetAmount", 1.0);
            lastWet = currentWet;

            // Step 3: Load and initialize JSFX with correct configuration
            auto jsfxPath = getCurrentJSFXPath();
            if (jsfxPath.isNotEmpty())
            {
                juce::File jsfxFile(jsfxPath);
                if (jsfxFile.existsAsFile())
                {
                    // Create JSFX instance without parameter mapping
                    unloadJSFX();

                    juce::File sourceDir = jsfxFile.getParentDirectory();
                    juce::String fileName = jsfxFile.getFileName();
                    bool wantWak = false;
                    sxInstance = JesusonicAPI.sx_createInstance(
                        sourceDir.getFullPathName().toRawUTF8(),
                        fileName.toRawUTF8(),
                        &wantWak
                    );

                    if (sxInstance)
                    {
                        apvts.state.setProperty(jsfxPathParamID, jsfxFile.getFullPathName(), nullptr);
                        sx_set_host_ctx(sxInstance, this, JsfxSliderAutomateThunk);
                        currentJSFXName = jsfxFile.getFileNameWithoutExtension();

                        // Set sample rate
                        JesusonicAPI
                            .sx_extended(sxInstance, JSFX_EXT_SET_SRATE, (void*)(intptr_t)lastSampleRate, nullptr);

                        // Get parameter count
                        numActiveParams = JesusonicAPI.sx_getNumParms(sxInstance);
                        numActiveParams = juce::jmin(numActiveParams, PluginConstants::MaxParameters);

                        // Step 4: Manually restore parameter values from APVTS to JSFX
                        // This is necessary because we're restoring a saved state, not loading fresh defaults
                        DBG("=== RESTORING PARAMETERS FROM STATE ===");
                        for (int i = 0; i < numActiveParams; ++i)
                        {
                            if (auto* param = parameterCache[i])
                            {
                                // Get parameter range from JSFX
                                double minVal, maxVal, step;
                                JesusonicAPI.sx_getParmVal(sxInstance, i, &minVal, &maxVal, &step);
                                parameterRanges[i].minVal = minVal;
                                parameterRanges[i].maxVal = maxVal;
                                parameterRanges[i].step = step;

                                // Get restored normalized value from APVTS
                                float normalizedValue = param->getValue();
                                DBG("Param " << i << ": normalizedValue=" << normalizedValue);

                                // Convert to actual JSFX value and set it
                                double actualValue =
                                    ParameterUtils::normalizedToActualValue(sxInstance, i, normalizedValue);
                                JesusonicAPI.sx_setParmVal(sxInstance, i, actualValue, 0);
                                DBG("  -> Set JSFX actual value: " << actualValue);
                            }
                        }

                        // Step 5: Initialize parameter sync manager (just snapshots current state, doesn't push)
                        parameterSync.initialize(parameterCache, sxInstance, numActiveParams, lastSampleRate);

                        // Set latency
                        int latencySamples = JesusonicAPI.sx_getCurrentLatency(sxInstance);
                        currentJSFXLatency.store(latencySamples, std::memory_order_relaxed);
                        setLatencySamples(latencySamples);

                        // Register MIDI callback
                        sx_set_midi_ctx(sxInstance, &midiSendRecvCallback, this);
                        DBG("MIDI context registered with JSFX instance: " << currentJSFXName);

                        // Check instrument flag
                        INT_PTR flags = JesusonicAPI.sx_extended(sxInstance, JSFX_EXT_GETFLAGS, nullptr, nullptr);
                        bool isInstrument = (flags & 1) != 0;
                        DBG("JSFX flags: " << flags << ", isInstrument=" << (isInstrument ? "YES" : "NO"));

                        // Scan for preset files (.rpl) in the JSFX directory
                        juce::Array<juce::File> presetDirs;
                        presetDirs.add(sourceDir);
                        DBG("Scanning for presets in: " << sourceDir.getFullPathName());
                        presetManager.scanDirectories(presetDirs);
                    }

                    // Step 6: Restore routing configuration
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

    // Note: Routing initialization moved to prepareToPlay() where bus layout is guaranteed valid

    // Register MIDI callback (not in jsfxAPI struct, call directly)
    sx_set_midi_ctx(sxInstance, &midiSendRecvCallback, this);
    DBG("MIDI context registered with JSFX instance: " << currentJSFXName);

    // Check if this JSFX is marked as an instrument (receives MIDI)
    INT_PTR flags = JesusonicAPI.sx_extended(sxInstance, JSFX_EXT_GETFLAGS, nullptr, nullptr);
    bool isInstrument = (flags & 1) != 0;
    DBG("JSFX flags: " << flags << ", isInstrument=" << (isInstrument ? "YES" : "NO"));

    // Scan for preset files (.rpl) in the JSFX directory and subdirectories
    juce::Array<juce::File> presetDirs;
    presetDirs.add(sourceDir); // Main JSFX directory

    DBG("Scanning for presets in: " << sourceDir.getFullPathName());
    presetManager.scanDirectories(presetDirs);

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

    // Read JSFX default values and update APVTS parameters
    // This initializes APVTS with the NEW JSFX's default state
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
            // Update APVTS with JSFX's default value (not the other way around!)
            float normalizedValue = ParameterUtils::actualToNormalizedValue(sxInstance, i, currentVal);
            param->setValueNotifyingHost(normalizedValue);
        }
    }

    // Initialize the parameter sync manager - it will now sync the JSFX defaults to audio thread
    parameterSync.initialize(parameterCache, sxInstance, numActiveParams, lastSampleRate);
}

//==============================================================================
// Preset Management

bool AudioPluginAudioProcessor::loadPresetByName(const juce::String& presetName)
{
    if (!sxInstance || presetName.isEmpty())
        return false;

    DBG("Loading preset: " << presetName);

    const auto* preset = presetManager.getPreset(presetName);
    if (!preset)
    {
        DBG("Preset not found: " << presetName);
        return false;
    }

    // Decode base64 data
    juce::MemoryOutputStream decodedStream;
    if (!juce::Base64::convertFromBase64(decodedStream, preset->data))
    {
        DBG("Failed to decode preset data");
        return false;
    }

    // Get the decoded binary data
    const void* presetData = decodedStream.getData();
    size_t presetDataSize = decodedStream.getDataSize();

    if (presetDataSize == 0)
    {
        DBG("Decoded preset data is empty");
        return false;
    }

    DBG("Decoded preset data: " << presetDataSize << " bytes");

    // Apply preset data to JSFX instance
    // The binary format is JSFX-specific parameter state
    // We need to parse it and set parameters using sx_setParmVal

    // JSFX preset format (based on REAPER's implementation):
    // - 4 bytes: number of parameters (int32)
    // - For each parameter: 8 bytes (double value)

    const unsigned char* dataPtr = static_cast<const unsigned char*>(presetData);

    if (presetDataSize < 4)
    {
        DBG("Preset data too small");
        return false;
    }

    // Read number of parameters (little-endian int32)
    int numPresetParams = static_cast<int>(dataPtr[0] | (dataPtr[1] << 8) | (dataPtr[2] << 16) | (dataPtr[3] << 24));

    DBG("Preset contains " << numPresetParams << " parameters");

    if (numPresetParams <= 0 || numPresetParams > PluginConstants::MaxParameters)
    {
        DBG("Invalid number of parameters in preset");
        return false;
    }

    size_t expectedSize = 4 + (numPresetParams * 8); // 4-byte count + 8 bytes per param
    if (presetDataSize < expectedSize)
    {
        DBG("Preset data size mismatch. Expected at least " << expectedSize << " bytes, got " << presetDataSize);
        return false;
    }

    // Apply parameters
    dataPtr += 4; // Skip the count
    for (int i = 0; i < numPresetParams && i < numActiveParams; ++i)
    {
        // Read double value (little-endian, IEEE 754)
        double value;
        std::memcpy(&value, dataPtr, sizeof(double));
        dataPtr += sizeof(double);

        // Set parameter in JSFX
        JesusonicAPI.sx_setParmVal(sxInstance, i, value, lastSampleRate);

        // Update APVTS to match
        if (auto* param = parameterCache[i])
        {
            // Convert JSFX value [min, max] to normalized [0, 1]
            double minVal = parameterRanges[i].minVal;
            double maxVal = parameterRanges[i].maxVal;
            float normalizedValue =
                (maxVal != minVal) ? static_cast<float>((value - minVal) / (maxVal - minVal)) : 0.0f;

            param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalizedValue));
        }
    }

    // Re-sync the parameter manager with new values
    parameterSync.initialize(parameterCache, sxInstance, numActiveParams, lastSampleRate);

    DBG("Preset loaded successfully: " << presetName);
    return true;
}

bool AudioPluginAudioProcessor::loadPresetFromData(const juce::String& base64Data)
{
    if (!sxInstance || base64Data.isEmpty())
        return false;

    DBG("Loading preset from data (" << base64Data.length() << " chars)");
    DBG("Base64 data: " << base64Data);

    // Decode base64 data to text
    juce::MemoryOutputStream decodedStream;
    bool decodeSuccess = juce::Base64::convertFromBase64(decodedStream, base64Data);

    DBG("Decode result: " << (decodeSuccess ? "SUCCESS" : "FAILED"));
    DBG("Decoded stream size: " << decodedStream.getDataSize() << " bytes");

    // Check if we got any data regardless of return value
    if (decodedStream.getDataSize() == 0)
    {
        DBG("No decoded data - preset data might be invalid");
        return false;
    }

    // Convert decoded data to string (JSFX text state format)
    juce::String stateText = decodedStream.toString();

    DBG("Decoded state text (" << stateText.length() << " chars): " << stateText.substring(0, 100) << "...");

    if (stateText.isEmpty())
    {
        DBG("Decoded preset text is empty");
        return false;
    }

    // Use JSFX API to load text state
    JesusonicAPI.sx_loadState(sxInstance, stateText.toRawUTF8());

    // Sync APVTS parameters with the loaded state
    for (int i = 0; i < numActiveParams; ++i)
    {
        if (auto* param = parameterCache[i])
        {
            double minVal, maxVal, step;
            double value = JesusonicAPI.sx_getParmVal(sxInstance, i, &minVal, &maxVal, &step);

            // Convert JSFX value [min, max] to normalized [0, 1]
            float normalizedValue =
                (maxVal != minVal) ? static_cast<float>((value - minVal) / (maxVal - minVal)) : 0.0f;

            param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalizedValue));
        }
    }

    // Re-sync the parameter manager with new values
    parameterSync.initialize(parameterCache, sxInstance, numActiveParams, lastSampleRate);

    DBG("Preset loaded successfully from data using sx_loadState");
    return true;
}

const char* AudioPluginAudioProcessor::getPresetNamesRaw()
{
    if (!sxInstance)
        return nullptr;

    // Get presets for the current JSFX effect
    auto presetNames = presetManager.getAllPresetNames(currentJSFXName);

    if (presetNames.isEmpty())
        return nullptr;

    // Build newline-separated list
    juce::StringArray formattedNames;
    for (const auto& name : presetNames)
        formattedNames.add(name);

    // Cache the string (JSFX expects the pointer to remain valid)
    presetNamesCache = formattedNames.joinIntoString("\n");

    DBG("Returning " << presetNames.size() << " preset names for: " << currentJSFXName);

    return presetNamesCache.toRawUTF8();
}

//==============================================================================
// Preset Host Callbacks (called by JSFX via getHostAPIFunction)

bool hostLoadReaperPreset(void* hostctx, const char* presetName)
{
    if (!hostctx || !presetName)
        return false;

    DBG("JSFX requesting to load preset: " << presetName);

    // Cast hostctx back to AudioPluginAudioProcessor
    auto* processor = static_cast<AudioPluginAudioProcessor*>(hostctx);

    // Forward to processor's preset loading logic
    return processor->loadPresetByName(juce::String(presetName));
}

const char* hostGetReaperPresetNamesRaw(void* hostctx)
{
    if (!hostctx)
        return nullptr;

    DBG("JSFX requesting preset names list");

    // Cast hostctx back to AudioPluginAudioProcessor
    auto* processor = static_cast<AudioPluginAudioProcessor*>(hostctx);

    // Forward to processor's preset names getter
    return processor->getPresetNamesRaw();
}

//==============================================================================
// MIDI callback for JSFX - called during sx_processSamples
double AudioPluginAudioProcessor::midiSendRecvCallback(
    void* ctx,
    int action,
    double* ts,
    double* msg1,
    double* msg23,
    double* midibus
)
{
    auto* processor = static_cast<AudioPluginAudioProcessor*>(ctx);
    if (!processor)
        return 0.0;

    juce::ignoreUnused(midibus); // Not handling multi-bus MIDI yet

    if (action == 0x100) // JSFX sends MIDI out (to host)
    {
        // Protocol: JSFX needs buffer to write MIDI output
        // 1. JSFX calls us with required length in msg1
        // 2. We allocate buffer and return pointer via msg23
        // 3. We return from callback
        // 4. JSFX writes MIDI data to buffer
        // 5. JSFX function returns (but we've already returned from callback!)
        //
        // Problem: We can't add to output buffer after JSFX writes since callback already returned.
        // Solution: JSFX actually expects us to just provide a buffer. The _midi_send_str function
        // immediately writes after our callback returns, so the data is there.
        // But we can't process it in this callback context.
        //
        // Workaround: Check REAPER source or other hosts... Actually, I think the pattern is:
        // We provide a persistent buffer, JSFX writes to it, and we process it later.
        // But that's complex. Let me try a simpler approach: Pre-allocate buffer and process
        // it immediately after return using a flag.

        if (!msg1 || !msg23 || !ts)
            return 0.0;

        int length = static_cast<int>(*msg1);
        if (length <= 0 || length > 8192) // Sanity check
            return 0.0;

        double timestamp = *ts;
        int samplePosition = static_cast<int>(timestamp + 0.5);

        // Allocate buffer for JSFX to write to
        processor->midiTempBuffer.resize(length);
        unsigned char* buffer = processor->midiTempBuffer.data();

        // Return buffer pointer to JSFX
        *reinterpret_cast<unsigned char**>(msg23) = buffer;

        // Return success - JSFX will now write to buffer
        // NOTE: After this returns, JSFX writes to buffer, but we can't process it here.
        // This is a fundamental design issue with the callback API.
        // For now, returning 0 to indicate we can't handle output yet.
        // TODO: Implement proper buffering mechanism
        return 0.0; // Disabled for now - need better implementation
    }
    else if (action < 0) // JSFX requests next MIDI event (action < 0 per VST2 implementation)
    {
        // Match VST2 implementation: Iterate through MIDI buffer sequentially
        // JSFX calls this repeatedly to get all MIDI events one by one
        // Returns timestamp in *ts, status in *msg1, data bytes in *msg23

        if (!processor->currentMidiInputBuffer || !processor->midiInputIterator || !msg1 || !msg23 || !ts)
            return 0.0;

        // Get next MIDI message from iterator
        juce::MidiMessage message;
        int samplePosition;

        if (processor->midiInputIterator->getNextEvent(message, samplePosition))
        {
            // Return MIDI data in JSFX format:
            // *ts = sample position (deltaFrames)
            // *msg1 = status byte
            // *msg23 = data bytes packed as (data1 + (data2 << 8))

            *ts = static_cast<double>(samplePosition);

            const uint8_t* rawData = message.getRawData();
            int numBytes = message.getRawDataSize();

            if (numBytes >= 1)
            {
                *msg1 = static_cast<double>(rawData[0]); // Status byte

                int data1 = (numBytes >= 2) ? rawData[1] : 0;
                int data2 = (numBytes >= 3) ? rawData[2] : 0;
                *msg23 = static_cast<double>(data1 + (data2 << 8));

                return 1.0; // Success - event available
            }
        }

        // No more MIDI events
        return 0.0;
    }

    return 0.0;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
