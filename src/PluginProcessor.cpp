#include "PluginProcessor.h"

#include "JsfxHelper.h"
#include "ParameterUtils.h"
#include "PluginEditor.h"
#include "FileIO.h"

#include <algorithm>

// LICE includes for GFX initialization
#define EEL_LICE_STANDALONE_NOINITQUIT
#define EEL_LICE_WANT_STANDALONE
#define EEL_LICE_API_ONLY
#include "WDL/eel2/eel_lice.h"

// Forward declaration for JSFX MIDI function (not in jsfxAPI struct but exists in effectproc.cpp)
extern void sx_set_midi_ctx(
    SX_Instance* sx,
    double (*midi_sendrecv)(void* ctx, int action, double* ts, double* msg1, double* msg23, double* midibus),
    void* midi_ctxdata
);

// LICE image loader initialization (ensures PNG/JPG/GIF loading works)
extern "C" void LICE_InitializeImageLoaders();

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
    // Initialize LICE image loaders (PNG, JPG, GIF support)
    LICE_InitializeImageLoaders();

    // JsfxHelper constructor automatically initializes per-instance JSFX system

    // Set slider class name for JSFX controls
    extern const char* g_config_slider_classname;
    g_config_slider_classname = "jsfx_slider";

    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    appDataDir = appDataDir.getChildFile(JucePlugin_Name);
    if (!FileIO::exists(appDataDir))
    {
        FileIO::createDirectory(appDataDir.getChildFile("Data"));
        FileIO::createDirectory(appDataDir.getChildFile("Effects"));
    }

    jsfxRootDir = appDataDir.getFullPathName();

    // Note: Global properties for directory management now handled by PersistentFileChooser

    // Populate parameter cache
    for (int i = 0; i < PluginConstants::MaxParameters; ++i)
    {
        auto paramID = juce::String("param") + juce::String(i);
        parameterCache[i] = apvts.getParameter(paramID);
    }

    // Initialize preset loader with preset cache
    presetLoader = std::make_unique<PresetLoader>(apvts, presetCache);

    // Start timer for latency updates and parameter sync (30 Hz = ~33ms)
    startTimer(33);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    // Stop timer first to prevent any callbacks during destruction
    stopTimer();

    // Ensure all JSFX resources are cleaned up
    unloadJSFX();

    // Arrays don't need explicit clearing - they're automatically cleaned up
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

//==============================================================================
bool AudioPluginAudioProcessor::loadPresetFromBase64(const juce::String& base64Data)
{
    if (base64Data.isEmpty())

        return false;

    auto* instance = getSXInstancePtr();
    if (!instance)

        return false;

    // Decode base64 data to text
    juce::MemoryOutputStream decodedStream;
    juce::Base64::convertFromBase64(decodedStream, base64Data);

    // Check if we got any data (ignore return value - JUCE can return false even on successful decode)
    if (decodedStream.getDataSize() == 0)

        return false;

    // Convert decoded data to string (JSFX text state format)
    juce::String stateText = decodedStream.toString();

    if (stateText.isEmpty())

        return false;

    // Use JSFX API to load text state
    suspendProcessing(true); // Suspend audio processing during state load
    JesusonicAPI.sx_loadState(instance, stateText.toRawUTF8());
    suspendProcessing(false);

    // Sync APVTS parameters with the loaded state
    int numParams = JesusonicAPI.sx_getNumParms(instance);

    for (int i = 0; i < numParams && i < PluginConstants::MaxParameters; ++i)
    {
        double minVal, maxVal, step;
        double value = JesusonicAPI.sx_getParmVal(instance, i, &minVal, &maxVal, &step);

        // Convert JSFX value to normalized [0, 1]
        float normalizedValue = (maxVal != minVal) ? static_cast<float>((value - minVal) / (maxVal - minVal)) : 0.0f;
        normalizedValue = juce::jlimit(0.0f, 1.0f, normalizedValue);

        // Update APVTS parameter (this will update the UI automatically via attachments)
        auto paramID = juce::String("param") + juce::String(i);
        if (auto* param = apvts.getParameter(paramID))
            param->setValueNotifyingHost(normalizedValue);
    }

    return true;
}

juce::String AudioPluginAudioProcessor::getCurrentStateAsBase64() const
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return {};

    // Get state from JSFX using sx_saveState
    int stateLength = 0;
    const char* stateText = JesusonicAPI.sx_saveState(instance, &stateLength);

    if (!stateText || stateLength <= 0)
        return {};

    // Convert state text to base64
    juce::String stateString(stateText, stateLength);
    juce::MemoryOutputStream outStream;
    outStream.writeString(stateString);

    return juce::Base64::toBase64(outStream.getData(), outStream.getDataSize());
}

bool AudioPluginAudioProcessor::saveUserPreset(const juce::String& bankName, const juce::String& presetName)
{
    // Get current JSFX path
    juce::String jsfxPath = getCurrentJSFXPath();
    if (jsfxPath.isEmpty())
        return false;

    juce::File jsfxFile(jsfxPath);
    juce::String jsfxFilename = jsfxFile.getFileNameWithoutExtension();

    // Get current state as base64
    juce::String presetData = getCurrentStateAsBase64();
    if (presetData.isEmpty())
        return false;

    // Build user presets directory: <appdata>/juceSonic/data/user/<jsfx-filename>/
    auto userPresetsDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile(PluginConstants::ApplicationName)
                              .getChildFile(PluginConstants::DataDirectoryName)
                              .getChildFile(PluginConstants::UserPresetsDirectoryName)
                              .getChildFile(jsfxFilename);

    // Create directory if it doesn't exist
    if (!userPresetsDir.exists() && !userPresetsDir.createDirectory())
        return false;

    // Determine the target file name
    // Special case: if this is the default preset, use the default filename
    juce::String filename;
    if (bankName == PluginConstants::DefaultPresetBankName && presetName == PluginConstants::DefaultPresetName)
        filename = PluginConstants::DefaultPresetFileName;
    else
        filename = bankName + ".rpl";

    // Target file: user/<jsfx-filename>/<filename>
    auto presetFile = userPresetsDir.getChildFile(filename);

    // Load existing content or create new
    juce::String fileContent;
    bool bankExists = false;
    juce::String beforeBank, bankContent, afterBank;

    if (presetFile.existsAsFile())
    {
        fileContent = presetFile.loadFileAsString();

        // Find the bank in the file
        juce::String bankTag = "<REAPER_PRESET_LIBRARY `" + bankName + "`";
        int bankStart = fileContent.indexOf(bankTag);

        if (bankStart >= 0)
        {
            bankExists = true;
            beforeBank = fileContent.substring(0, bankStart);

            // Find the end of this bank (matching closing >)
            int openTagEnd = bankStart + bankTag.length();
            while (openTagEnd < fileContent.length() && fileContent[openTagEnd] != '>')
                openTagEnd++;

            if (openTagEnd < fileContent.length())
            {
                int depth = 1;
                int bankEnd = -1;
                const char* data = fileContent.toRawUTF8();

                for (int i = openTagEnd + 1; i < fileContent.length() && depth > 0; i++)
                {
                    char c = data[i];
                    if (c == '`' || c == '"' || c == '\'')
                    {
                        char quote = c;
                        i++;
                        while (i < fileContent.length() && data[i] != quote)
                            i++;
                        continue;
                    }
                    if (c == '<')
                        depth++;
                    else if (c == '>')
                    {
                        depth--;
                        if (depth == 0)
                        {
                            bankEnd = i;
                            break;
                        }
                    }
                }

                if (bankEnd >= 0)
                {
                    bankContent = fileContent.substring(bankStart, bankEnd + 1);
                    afterBank = fileContent.substring(bankEnd + 1);

                    // Check if preset already exists in this bank
                    juce::String presetTag = "<PRESET `" + presetName + "`";
                    int presetStart = bankContent.indexOf(presetTag);

                    if (presetStart >= 0)
                    {
                        // Replace existing preset
                        int presetTagEnd = presetStart + presetTag.length();
                        while (presetTagEnd < bankContent.length() && bankContent[presetTagEnd] != '>')
                            presetTagEnd++;

                        if (presetTagEnd < bankContent.length())
                        {
                            int pDepth = 1;
                            int presetEnd = -1;
                            const char* bankData = bankContent.toRawUTF8();

                            for (int i = presetTagEnd + 1; i < bankContent.length() && pDepth > 0; i++)
                            {
                                char c = bankData[i];
                                if (c == '`' || c == '"' || c == '\'')
                                {
                                    char quote = c;
                                    i++;
                                    while (i < bankContent.length() && bankData[i] != quote)
                                        i++;
                                    continue;
                                }
                                if (c == '<')
                                    pDepth++;
                                else if (c == '>')
                                {
                                    pDepth--;
                                    if (pDepth == 0)
                                    {
                                        presetEnd = i;
                                        break;
                                    }
                                }
                            }

                            if (presetEnd >= 0)
                            {
                                // Replace the preset
                                juce::String newPreset =
                                    "  <PRESET `" + presetName + "`\n    " + presetData + "\n  >\n";
                                bankContent = bankContent.substring(0, presetStart)
                                            + newPreset
                                            + bankContent.substring(presetEnd + 1);
                            }
                        }
                    }
                    else
                    {
                        // Add new preset to existing bank (before the closing >)
                        juce::String newPreset = "  <PRESET `" + presetName + "`\n    " + presetData + "\n  >\n";
                        bankContent = bankContent.substring(0, bankEnd) + newPreset + ">";
                    }
                }
            }
        }
    }

    // Build the final content
    if (bankExists)
    {
        fileContent = beforeBank + bankContent + afterBank;
    }
    else
    {
        // Create new bank
        juce::String newBank = "<REAPER_PRESET_LIBRARY `" + bankName + "`\n";
        newBank += "  <PRESET `" + presetName + "`\n    " + presetData + "\n  >\n";
        newBank += ">\n";

        if (fileContent.isEmpty())
            fileContent = newBank;
        else
            fileContent += "\n" + newBank;
    }

    // Write the file
    if (!presetFile.replaceWithText(fileContent))
        return false;

    // Trigger preset refresh
    if (presetLoader)
        presetLoader->requestRefresh(jsfxPath);

    return true;
}

void AudioPluginAudioProcessor::resetToDefaults()
{
    // Check if a default preset exists
    if (hasDefaultPreset())
    {
        // Load the default preset
        juce::String jsfxPath = getCurrentJSFXPath();
        if (jsfxPath.isEmpty())
            return;

        juce::File jsfxFile(jsfxPath);
        juce::String jsfxFilename = jsfxFile.getFileNameWithoutExtension();

        // Build path to default preset file
        auto defaultPresetFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                     .getChildFile(PluginConstants::ApplicationName)
                                     .getChildFile(PluginConstants::DataDirectoryName)
                                     .getChildFile(PluginConstants::UserPresetsDirectoryName)
                                     .getChildFile(jsfxFilename)
                                     .getChildFile(PluginConstants::DefaultPresetFileName);

        if (defaultPresetFile.existsAsFile())
        {
            // Use ReaperPresetConverter to find and extract the default preset
            juce::String presetData =
                ReaperPresetConverter::findPresetByName(defaultPresetFile, PluginConstants::DefaultPresetName);

            if (presetData.isNotEmpty())
            {
                loadPresetFromBase64(presetData);
                return;
            }
        }
    }

    // No default preset found - reset to JSFX parameter defaults
    if (!sxInstance)
        return;

    // To get true JSFX defaults, we need to reload the JSFX
    // Save the current JSFX path
    juce::String jsfxPath = getCurrentJSFXPath();
    if (jsfxPath.isEmpty())
        return;

    // Reload the JSFX to reset all parameters to their @init values
    juce::File jsfxFile(jsfxPath);
    if (jsfxFile.existsAsFile())
        loadJSFX(jsfxFile);
}

bool AudioPluginAudioProcessor::setAsDefaultPreset()
{
    // Save current state as default preset
    return saveUserPreset(PluginConstants::DefaultPresetBankName, PluginConstants::DefaultPresetName);
}

bool AudioPluginAudioProcessor::hasDefaultPreset() const
{
    juce::String jsfxPath = getCurrentJSFXPath();
    if (jsfxPath.isEmpty())
        return false;

    juce::File jsfxFile(jsfxPath);
    juce::String jsfxFilename = jsfxFile.getFileNameWithoutExtension();

    // Check if default preset file exists
    auto defaultPresetFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                 .getChildFile(PluginConstants::ApplicationName)
                                 .getChildFile(PluginConstants::DataDirectoryName)
                                 .getChildFile(PluginConstants::UserPresetsDirectoryName)
                                 .getChildFile(jsfxFilename)
                                 .getChildFile(PluginConstants::DefaultPresetFileName);

    return defaultPresetFile.existsAsFile();
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

    // TODO: Add MIDI Program Change support
    // To implement preset loading via MIDI Program Change:
    // 1. Iterate through midiMessages before passing to JSFX
    // 2. Check for isProgramChange() messages
    // 3. Get program number (0-127)
    // 4. Map program number to preset base64 data (from LibraryBrowser's ValueTree)
    // 5. Call loadPresetFromBase64(presetData) from message thread via MessageManager::callAsync
    //    (Never call directly from audio thread - it updates UI parameters!)
    // Example:
    //   for (auto metadata : midiMessages)
    //   {
    //       auto msg = metadata.getMessage();
    //       if (msg.isProgramChange())
    //       {
    //           int programNum = msg.getProgramChangeNumber();
    //           juce::MessageManager::callAsync([this, programNum]()
    //           {
    //               auto presetData = getPresetDataForProgram(programNum);
    //               loadPresetFromBase64(presetData);
    //           });
    //       }
    //   }

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
        1.0, // lastWet (always 100% wet)
        1.0, // currentWet (always 100% wet)
        0
    );

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
            // Restore the state tree (parameters and properties)
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));

            // Load JSFX from stored path (loadJSFX will handle all initialization)
            auto jsfxPath = getCurrentJSFXPath();
            DBG("setStateInformation: Restoring JSFX from path: " + jsfxPath);
            if (jsfxPath.isNotEmpty())
            {
                juce::File jsfxFile(jsfxPath);
                DBG("  File exists: " + juce::String(jsfxFile.existsAsFile() ? "YES" : "NO"));
                if (jsfxFile.existsAsFile())
                {
                    DBG("  Calling loadJSFX...");
                    bool success = loadJSFX(jsfxFile);
                    DBG("  loadJSFX returned: " + juce::String(success ? "SUCCESS" : "FAILED"));
                }
            }

            // Restore routing configuration
            auto routingStr = apvts.state.getProperty("ioMatrixRouting", "").toString();
            if (routingStr.isNotEmpty())
                restoreRoutingFromString(routingStr);
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

    // Update JSFX with the current channel count if instance exists
    if (sxInstance)
        JesusonicAPI.sx_updateHostNch(sxInstance, getTotalNumInputChannels());
}

bool AudioPluginAudioProcessor::loadJSFX(const juce::File& jsfxFile)
{
    if (!jsfxFile.existsAsFile())
        return false;

    // Create new instance from source directory (allows live updates and dependency resolution)
    juce::File sourceDir = jsfxFile.getParentDirectory();
    juce::String fileName = jsfxFile.getFileName();

    DBG("loadJSFX called with:");
    DBG("  File: " + jsfxFile.getFullPathName());
    DBG("  Source dir: " + sourceDir.getFullPathName());
    DBG("  Filename: " + fileName);

    // Check if file contains @gfx section
    juce::String fileContent = jsfxFile.loadFileAsString();
    bool fileHasGfxSection = fileContent.contains("@gfx");
    int gfxPosition = fileContent.indexOf("@gfx");
    DBG("  File contains @gfx: " + juce::String(fileHasGfxSection ? "YES" : "NO"));
    if (fileHasGfxSection)
        DBG("  @gfx position in file: " + juce::String(gfxPosition));
    DBG("  File size: " + juce::String(jsfxFile.getSize()) + " bytes");
    DBG("  First 200 chars: " + fileContent.substring(0, 200).replace("\n", "\\n").replace("\r", "\\r"));

    bool wantWak = false;
    SX_Instance* newInstance =
        JesusonicAPI.sx_createInstance(sourceDir.getFullPathName().toRawUTF8(), fileName.toRawUTF8(), &wantWak);

    if (!newInstance)
    {
        DBG("ERROR: Failed to create JSFX instance");
        return false;
    }

    DBG("JSFX instance created successfully");
    DBG("  Has GFX code: " + juce::String(newInstance->gfx_hasCode() ? "YES" : "NO"));

    // Setup new instance
    sx_set_host_ctx(newInstance, this, JsfxSliderAutomateThunk);
    JesusonicAPI.sx_extended(newInstance, JSFX_EXT_SET_SRATE, (void*)(intptr_t)lastSampleRate, nullptr);
    sx_set_midi_ctx(newInstance, &midiSendRecvCallback, this);
    JesusonicAPI.sx_updateHostNch(newInstance, getTotalNumInputChannels());

    // Initialize JSFX graphics (@gfx section) before swapping
    // This ensures the LICE state and framebuffer are ready when the UI accesses it
    if (newInstance->gfx_hasCode())
    {
        DBG("Initializing GFX for JSFX...");
        auto* liceState = newInstance->m_lice_state;
        if (liceState)
        {
            DBG("  LICE state exists");
            // If JSFX needs initialization, call on_slider_change()
            if (newInstance->m_need_init)
            {
                newInstance->m_mutex.Enter();
                newInstance->m_init_mutex.Enter();
                if (newInstance->m_need_init)
                    newInstance->on_slider_change();
                newInstance->m_mutex.Leave();
            }
            else
            {
                newInstance->m_init_mutex.Enter();
            }

            // Setup framebuffer with default dimensions (400x300)
            // This creates m_framebuffer and initializes gfx_w/gfx_h
            RECT r = {0, 0, 400, 300};
            if (liceState->setup_frame(nullptr, r) >= 0)
            {
                // Trigger initial @gfx execution
                newInstance->gfx_runCode(0);
            }

            newInstance->m_init_mutex.Leave();
        }
    }

    int latencySamples = JesusonicAPI.sx_getCurrentLatency(newInstance);

    // Atomically swap instances while audio thread is suspended
    // Both loadJSFX and JsfxLiceComponent run on message thread, so no contention there
    suspendProcessing(true);
    SX_Instance* oldInstance = sxInstance;
    sxInstance = newInstance;
    suspendProcessing(false);

    // Destroy old instance after swap
    if (oldInstance)
    {
        JesusonicAPI.sx_destroyInstance(oldInstance);
        parameterSync.reset();
    }

    // Update state and parameters
    apvts.state.setProperty(jsfxPathParamID, jsfxFile.getFullPathName(), nullptr);
    updateParameterMapping();
    currentJSFXLatency.store(latencySamples, std::memory_order_relaxed);
    setLatencySamples(latencySamples);

    // Get effect name and author
    const char* description = sxInstance->m_description.Get();

    if (description && description[0] != '\0')
        currentJSFXName = juce::String::fromUTF8(description);
    else
    {
        const char* effectName = JesusonicAPI.sx_getEffectName(sxInstance);
        currentJSFXName = (effectName && effectName[0] != '\0') ? juce::String::fromUTF8(effectName)
                                                                : jsfxFile.getFileNameWithoutExtension();
    }

    currentJSFXAuthor = JsfxHelper::parseJSFXAuthor(jsfxFile);

    // Trigger preset refresh
    if (presetLoader)
        presetLoader->requestRefresh(jsfxFile.getFullPathName());

    return true;
}

void AudioPluginAudioProcessor::unloadJSFX()
{
    if (!sxInstance)
        return;

    // Atomically clear instance while audio thread is suspended
    // Both unloadJSFX and JsfxLiceComponent run on message thread, so no contention there
    suspendProcessing(true);
    SX_Instance* oldInstance = sxInstance;
    sxInstance = nullptr;
    suspendProcessing(false);

    // Destroy old instance and reset state
    JesusonicAPI.sx_destroyInstance(oldInstance);
    parameterSync.reset();

    currentJSFXLatency.store(0, std::memory_order_relaxed);
    setLatencySamples(0);

    apvts.state.setProperty(jsfxPathParamID, "", nullptr);
    currentJSFXName.clear();
    currentJSFXAuthor.clear();
    numActiveParams = 0;

    if (presetLoader)
        presetLoader->requestRefresh("");
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

    // Get parameter ranges from JSFX and sync APVTS values INTO JSFX
    // This preserves any restored APVTS state (from setStateInformation)
    for (int i = 0; i < numActiveParams; ++i)
    {
        // Get parameter range information from JSFX
        double jsfxDefaultVal = JesusonicAPI.sx_getParmVal(
            sxInstance,
            i,
            &parameterRanges[i].minVal,
            &parameterRanges[i].maxVal,
            &parameterRanges[i].step
        );

        if (auto* param = parameterCache[i])
        {
            // Get current APVTS value (may be restored from saved state or default)
            float normalizedValue = param->getValue();

            // Convert normalized value to actual JSFX range
            double actualValue = ParameterUtils::normalizedToActualValue(sxInstance, i, normalizedValue);

            // Set the JSFX parameter to match APVTS (preserves restored state)
            // sampleoffs=0 means apply immediately
            JesusonicAPI.sx_setParmVal(sxInstance, i, actualValue, 0);

            DBG("Param "
                << i
                << " synced to JSFX: normalizedVal="
                << juce::String(normalizedValue, 3)
                << " actualVal="
                << juce::String(actualValue, 3)
                << " range=["
                << juce::String(parameterRanges[i].minVal, 3)
                << ".."
                << juce::String(parameterRanges[i].maxVal, 3)
                << "]");
        }
    }

    // Initialize the parameter sync manager with current APVTS state
    parameterSync.initialize(parameterCache, sxInstance, numActiveParams, lastSampleRate);
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
