#include "PluginProcessor.h"

#include "PluginEditor.h"

#ifdef _WIN32
#include <commctrl.h>
#include <windows.h>
#endif

// WDL localization (required for JSFX UI dialogs)
#include "build/_deps/jsfx-src/WDL/localize/localize.h"
// JSFX UI host integration (host context, UI creation)
#include "build/_deps/jsfx-src/jsfx/sfxui.h"
// JUCE binary data for slider bitmap
#include "BinaryData.h"

#ifdef _WIN32
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

// Create HBITMAP from JUCE binary data for JSFX slider thumb
static HBITMAP CreateBitmapFromJUCEBinaryData(const void* data, int dataSize)
{
    // Create a memory stream from the binary data
    juce::MemoryInputStream stream(data, dataSize, false);

    // Load the image from the stream
    auto image = juce::ImageFileFormat::loadFrom(stream);
    if (!image.isValid())
        return nullptr;

    // Convert JUCE Image to Windows HBITMAP
    // Create a compatible DC and bitmap
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = image.getWidth();
    bmi.bmiHeader.biHeight = -image.getHeight(); // Negative for top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bitmapData;
    HBITMAP hBitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bitmapData, nullptr, 0);

    if (hBitmap && bitmapData)
    {
        // Copy pixel data from JUCE Image to Windows bitmap
        juce::Image::BitmapData imgData(image, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < image.getHeight(); ++y)
        {
            uint32_t* destRow = static_cast<uint32_t*>(bitmapData) + (y * image.getWidth());
            for (int x = 0; x < image.getWidth(); ++x)
            {
                juce::Colour pixel = imgData.getPixelColour(x, y);
                // Convert ARGB to BGRA for Windows bitmap
                destRow[x] =
                    (pixel.getAlpha() << 24) | (pixel.getRed() << 16) | (pixel.getGreen() << 8) | pixel.getBlue();
            }
        }
    }

    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    return hBitmap;
}
#endif

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
    // Get the module handle of THIS DLL
    g_hInst = (HINSTANCE)juce::Process::getCurrentModuleInstanceHandle();

#ifdef _WIN32
    // Initialize WDL localization system (required for JSFX UI dialogs)
    WDL_LoadLanguagePack("", NULL);
    DBG("WDL localization initialized");

    // Initialize common controls for the JSFX UI dialog
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    // Register and use the custom JSFX slider control class.
    // This matches sfxui.cpp expectations (uses extra window bytes and TBM_* messages).
    extern void Sliders_Init(HINSTANCE hInst, bool reg, int hslider_bitmap_id);
    extern void Sliders_SetBitmap(HBITMAP hBitmap, bool isVert);

    Sliders_Init(g_hInst, true, 0);
    g_config_slider_classname = "jsfx_slider";

    // Create and set the slider bitmap from JUCE binary data
    HBITMAP sliderBitmap =
        CreateBitmapFromJUCEBinaryData(BinaryData::cockos_hslider_bmp, BinaryData::cockos_hslider_bmpSize);
    if (sliderBitmap)
    {
        Sliders_SetBitmap(sliderBitmap, false); // Set horizontal slider bitmap
        // Note: We could also set a vertical bitmap if needed:
        // Sliders_SetBitmap(sliderBitmap, true);
        DBG("Successfully loaded slider bitmap from JUCE binary data");
    }
    else
    {
        DBG("Warning: Failed to create slider bitmap from JUCE binary data");
    }

    // Register stub window classes for JSFX custom controls
    // These are normally provided by REAPER, but we need to register them ourselves
    // Use ANSI version since JSFX dialog templates use ANSI strings
    // Register globally (CS_GLOBALCLASS) so they work in child dialogs
    WNDCLASSA wc = {0};
    wc.style = CS_GLOBALCLASS;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    // Register REAPERknob class (knob control)
    wc.lpszClassName = "REAPERknob";
    RegisterClassA(&wc);

    // Register REAPERvertvu class (VU meter)
    wc.lpszClassName = "REAPERvertvu";
    RegisterClassA(&wc);

    // Register WDLCursesWindow class (debug window)
    wc.lpszClassName = "WDLCursesWindow";
    RegisterClassA(&wc);

    DBG("JSFX initialization complete");

#endif

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

    // Start timer for latency updates (check every 100ms)
    startTimer(100);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    stopTimer();
    unloadJSFX();
}

//==============================================================================
void AudioPluginAudioProcessor::timerCallback()
{
    // Check if latency has changed and update the host
    int latency = currentJSFXLatency.load(std::memory_order_relaxed);
    if (latency != getLatencySamples())
        setLatencySamples(latency);
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

    // Update latency atomically for the timer to read (some JSFX can have dynamic latency)
    currentJSFXLatency.store(JesusonicAPI.sx_getCurrentLatency(sxInstance), std::memory_order_relaxed);

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

    // Provide host context and slider automate callback for UI
    // Use 'this' as host context so we can route callbacks if needed
    sx_set_host_ctx(sxInstance, this, JsfxSliderAutomateThunk);

    currentJSFXName = effectName;

    JesusonicAPI.sx_extended(sxInstance, JSFX_EXT_SET_SRATE, (void*)(intptr_t)lastSampleRate, nullptr);

    updateParameterMapping();

    // Get and set initial latency
    int latencySamples = JesusonicAPI.sx_getCurrentLatency(sxInstance);
    currentJSFXLatency.store(latencySamples, std::memory_order_relaxed);
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
        currentJSFXLatency.store(0, std::memory_order_relaxed);
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
