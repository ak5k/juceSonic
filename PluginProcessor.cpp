#include "PluginProcessor.h"

#include "PluginEditor.h"

// WDL localization
#include "jsfx/WDL/localize/localize.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < maxParameters; ++i)
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
#endif
      )
    , apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    g_hInst = (HINSTANCE)juce::Process::getCurrentModuleInstanceHandle();

    // Initialize WDL localization system (required for JSFX UI)
    WDL_LoadLanguagePack("", NULL);

    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    appDataDir = appDataDir.getChildFile(JucePlugin_Name);
    if (!appDataDir.exists())
    {
        appDataDir.getChildFile("Data").createDirectory();
        appDataDir.getChildFile("Effects").createDirectory();
    }

    jsfxRootDir = appDataDir.getFullPathName();

    parameterCache.reserve(maxParameters);
    for (int i = 0; i < maxParameters; ++i)
    {
        auto paramID = juce::String("param") + juce::String(i);
        parameterCache.push_back(apvts.getParameter(paramID));
    }
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    unloadJSFX();
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
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
#if !JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);

    juce::ScopedNoDenormals noDenormals;
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    if (!sxInstance)
    {
        buffer.clear();
        return;
    }

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();
    tempBuffer.setSize(1, numSamples * numChannels, false, false, true);
    auto* tempPtr = tempBuffer.getWritePointer(0);

    for (int sample = 0; sample < numSamples; ++sample)
        for (int channel = 0; channel < numChannels; ++channel)
            tempPtr[sample * numChannels + channel] = buffer.getReadPointer(channel)[sample];

    static std::vector<float> lastValues(maxParameters, -999.0f);
    static std::vector<double> lastActualValues(maxParameters, -999999.0);

    for (int i = 0; i < numActiveParams; ++i)
    {
        if (auto* param = parameterCache[i])
        {
            float normalizedValue = param->getValue();

            // Get range fresh each time (like VST wrapper does) - range might be dynamic!
            double minVal, maxVal;
            JesusonicAPI.sx_getParmVal(sxInstance, i, &minVal, &maxVal, NULL);
            double actualValue = minVal + normalizedValue * (maxVal - minVal);

            // Only send to JSFX if value actually changed
            if (std::abs(actualValue - lastActualValues[i]) > 0.0001)
            {
                // Use sampleoffs=0 like VST wrapper does
                JesusonicAPI.sx_setParmVal(sxInstance, i, actualValue, 0);
                lastActualValues[i] = actualValue;

                // Debug: confirm we're actually calling sx_setParmVal
                if (i < 2)
                {
                    DBG("*** sx_setParmVal called for param "
                        << i
                        << " with value "
                        << actualValue
                        << " (sampleoffs=0 - immediate)");
                }
            }

            // Debug output when parameter values change (only for first 2 params)
            if (i < 2 && std::abs(normalizedValue - lastValues[i]) > 0.001f)
            {
                // Verify what's actually stored after setting
                double verifyMin, verifyMax, verifyStep;
                double storedValue = JesusonicAPI.sx_getParmVal(sxInstance, i, &verifyMin, &verifyMax, &verifyStep);

                DBG("Param "
                    << i
                    << ": normalized="
                    << normalizedValue
                    << " range=["
                    << minVal
                    << ".."
                    << maxVal
                    << "] actual="
                    << actualValue
                    << " -> sent to JSFX"
                    << " | Stored: "
                    << storedValue
                    << " (min="
                    << verifyMin
                    << " max="
                    << verifyMax
                    << " step="
                    << verifyStep
                    << ")");
                lastValues[i] = normalizedValue;
            }
        }
    }

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
        totalNumOutputChannels,
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

    // Check for latency changes (some JSFX can have dynamic latency)
    int currentLatency = JesusonicAPI.sx_getCurrentLatency(sxInstance);
    if (currentLatency != getLatencySamples())
        setLatencySamples(currentLatency);

    for (int sample = 0; sample < numSamples; ++sample)
        for (int channel = 0; channel < numChannels; ++channel)
            buffer.getWritePointer(channel)[sample] = tempPtr[sample * numChannels + channel];

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    // for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    //     buffer.clear(i, 0, buffer.getNumSamples());

    // // This is the place where you'd normally do the guts of your plugin's
    // // audio processing...
    // // Make sure to reset the state if your inner loop is processing
    // // the samples and the outer loop is handling the channels.
    // // Alternatively, you can process the samples with the channels
    // // interleaved by keeping the same state.
    // for (int channel = 0; channel < totalNumInputChannels; ++channel)
    // {
    //     auto* channelData = buffer.getWritePointer(channel);
    //     juce::ignoreUnused(channelData);
    //     // ..do something to the data...
    // }
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
                    loadJSFX(jsfxFile);
            }
        }
    }
}

bool AudioPluginAudioProcessor::loadJSFX(const juce::File& jsfxFile)
{
    if (!jsfxFile.existsAsFile())
        return false;

    unloadJSFX();

    auto effectName = jsfxFile.getFileNameWithoutExtension();
    juce::File appDataDir = juce::File(jsfxRootDir);
    juce::File effectsDir = appDataDir.getChildFile("Effects");

    if (!effectsDir.exists())
        effectsDir.createDirectory();

    // Always copy to filename without extension (JSFX API expects no extension)
    juce::File targetFile = effectsDir.getChildFile(effectName);

    if (jsfxFile != targetFile)
        jsfxFile.copyFileTo(targetFile);

    bool wantWak = false;
    sxInstance = JesusonicAPI.sx_createInstance(
        appDataDir.getFullPathName().toRawUTF8(),
        ("Effects/" + effectName).toRawUTF8(),
        &wantWak
    );

    if (!sxInstance)
        return false;

    apvts.state.setProperty(jsfxPathParamID, jsfxFile.getFullPathName(), nullptr);

    currentJSFXName = effectName;

    JesusonicAPI.sx_extended(sxInstance, JSFX_EXT_SET_SRATE, (void*)(intptr_t)lastSampleRate, nullptr);

    updateParameterMapping();

    // Get and report latency to host
    int latencySamples = JesusonicAPI.sx_getCurrentLatency(sxInstance);
    setLatencySamples(latencySamples);

    return true;
}

void AudioPluginAudioProcessor::unloadJSFX()
{
    if (sxInstance)
    {
        JesusonicAPI.sx_destroyInstance(sxInstance);
        sxInstance = nullptr;

        // Reset latency to 0 when unloading
        setLatencySamples(0);
    }

    apvts.state.setProperty(jsfxPathParamID, "", nullptr);

    currentJSFXName.clear();
    numActiveParams = 0;
}

juce::String AudioPluginAudioProcessor::getCurrentJSFXPath() const
{
    return apvts.state.getProperty(jsfxPathParamID, "").toString();
}

juce::String AudioPluginAudioProcessor::getJSFXParameterName(int index) const
{
    if (!sxInstance || index < 0 || index >= numActiveParams)
        return "Parameter " + juce::String(index);

    char paramName[256] = {0};
    JesusonicAPI.sx_getParmName(sxInstance, index, paramName, sizeof(paramName));

    if (paramName[0] != 0)
        return juce::String(paramName);

    return "Parameter " + juce::String(index);
}

bool AudioPluginAudioProcessor::getJSFXParameterRange(int index, double& minVal, double& maxVal, double& step) const
{
    if (index < 0 || index >= static_cast<int>(parameterRanges.size()))
        return false;

    minVal = parameterRanges[index].minVal;
    maxVal = parameterRanges[index].maxVal;
    step = parameterRanges[index].step;
    return true;
}

bool AudioPluginAudioProcessor::isJSFXParameterEnum(int index) const
{
    if (!sxInstance || index < 0 || index >= numActiveParams)
        return false;

    return JesusonicAPI.sx_parmIsEnum(sxInstance, index) != 0;
}

juce::String AudioPluginAudioProcessor::getJSFXParameterDisplayText(int index, double value) const
{
    if (!sxInstance || index < 0 || index >= numActiveParams)
        return juce::String(value);

    char displayText[256] = {0};
    JesusonicAPI.sx_getParmDisplay(sxInstance, index, displayText, sizeof(displayText), &value);

    if (displayText[0] != 0)
        return juce::String(displayText);

    return juce::String(value);
}

void AudioPluginAudioProcessor::updateParameterMapping()
{
    if (!sxInstance)
    {
        numActiveParams = 0;
        parameterRanges.clear();
        return;
    }

    numActiveParams = JesusonicAPI.sx_getNumParms(sxInstance);
    numActiveParams = juce::jmin(numActiveParams, maxParameters);

    parameterRanges.resize(numActiveParams);

    for (int i = 0; i < numActiveParams; ++i)
    {
        double currentVal = JesusonicAPI.sx_getParmVal(
            sxInstance,
            i,
            &parameterRanges[i].minVal,
            &parameterRanges[i].maxVal,
            &parameterRanges[i].step
        );

        DBG("Param "
            << i
            << " from JSFX: currentVal="
            << currentVal
            << " range=["
            << parameterRanges[i].minVal
            << ".."
            << parameterRanges[i].maxVal
            << "]"
            << " step="
            << parameterRanges[i].step);

        if (auto* param = parameterCache[i])
        {
            float normalizedValue = 0.0f;
            if (parameterRanges[i].maxVal > parameterRanges[i].minVal)
            {
                normalizedValue = static_cast<float>(
                    (currentVal - parameterRanges[i].minVal) / (parameterRanges[i].maxVal - parameterRanges[i].minVal)
                );
            }

            param->setValueNotifyingHost(normalizedValue);
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
