/*
  Custom Standalone Application
  Based on JUCE's juce_audio_plugin_client_Standalone.cpp
  Modified to apply JuceSonicLookAndFeel globally
*/

#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone

#if !JUCE_MODULE_AVAILABLE_juce_audio_utils
#error To compile AudioUnitv3 and/or Standalone plug-ins, you need to add the juce_audio_utils and juce_audio_devices modules!
#endif

#include <juce_core/system/juce_TargetPlatform.h>
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>

#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_gui_basics/native/juce_WindowsHooks_windows.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

// Include standalone window
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

// Include our custom LookAndFeel - separate minimal header to avoid namespace issues
#include "JuceSonicLookAndFeel.h"
#include "FileIO.h"

namespace juce
{

//==============================================================================
class CustomStandaloneFilterWindow : public StandaloneFilterWindow
{
public:
    CustomStandaloneFilterWindow(
        const juce::String& title,
        juce::Colour backgroundColour,
        std::unique_ptr<StandalonePluginHolder> holder
    )
        : StandaloneFilterWindow(title, backgroundColour, std::move(holder))
    {
        // LookAndFeel is set globally by the app's SharedResourcePointer<SharedJuceSonicLookAndFeel>

        // Use JUCE's custom title bar instead of native OS title bar
        // This allows full customization of the title bar and buttons
        setUsingNativeTitleBar(false);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomStandaloneFilterWindow)
};

//==============================================================================
class CustomStandaloneFilterApp final : public JUCEApplication
{
public:
    CustomStandaloneFilterApp()
    {
        PropertiesFile::Options options;

        options.applicationName = CharPointer_UTF8(JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
#else
        options.folderName = "";
#endif

        // Protect with global file lock to prevent conflicts between multiple instances
        FileIO::ScopedFileLock lock;
        appProperties.setStorageParameters(options);
    }

    const String getApplicationName() override
    {
        return CharPointer_UTF8(JucePlugin_Name);
    }

    const String getApplicationVersion() override
    {
        return JucePlugin_VersionString;
    }

    bool moreThanOneInstanceAllowed() override
    {
        return true;
    }

    void anotherInstanceStarted(const String&) override
    {
    }

    CustomStandaloneFilterWindow* createWindow()
    {
        if (Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            // No displays are available, so no window will be created!
            jassertfalse;
            return nullptr;
        }

        return new CustomStandaloneFilterWindow(
            getApplicationName(),
            LookAndFeel::getDefaultLookAndFeel().findColour(ResizableWindow::backgroundColourId),
            createPluginHolder()
        );
    }

    std::unique_ptr<StandalonePluginHolder> createPluginHolder()
    {
        constexpr auto autoOpenMidiDevices =
#if (JUCE_ANDROID || JUCE_IOS) && !JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE
            true;
#else
            false;
#endif

#ifdef JucePlugin_PreferredChannelConfigurations
        constexpr StandalonePluginHolder::PluginInOuts channels[]{JucePlugin_PreferredChannelConfigurations};
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig(channels, juce::numElementsInArray(channels));
#else
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig;
#endif

        return std::make_unique<StandalonePluginHolder>(
            appProperties.getUserSettings(),
            false,
            String{},
            nullptr,
            channelConfig,
            autoOpenMidiDevices
        );
    }

    //==============================================================================
    void initialise(const String&) override
    {
        mainWindow = rawToUniquePtr(createWindow());

        if (mainWindow != nullptr)
        {
#if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
            Desktop::getInstance().setKioskModeComponent(mainWindow.get(), false);
#endif

            mainWindow->setVisible(true);
        }
        else
        {
            pluginHolder = createPluginHolder();
        }
    }

    void shutdown() override
    {
        pluginHolder = nullptr;
        mainWindow = nullptr;

        // Protect with global file lock to prevent conflicts between multiple instances
        FileIO::ScopedFileLock lock;
        appProperties.saveIfNeeded();
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        if (pluginHolder != nullptr)
            pluginHolder->savePluginState();

        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay(
                100,
                []()
                {
                    if (auto app = JUCEApplicationBase::getInstance())
                        app->systemRequestedQuit();
                }
            );
        }
        else
        {
            quit();
        }
    }

protected:
    ApplicationProperties appProperties;
    std::unique_ptr<CustomStandaloneFilterWindow> mainWindow;
    juce::SharedResourcePointer<SharedJuceSonicLookAndFeel> lookAndFeel;

private:
    std::unique_ptr<StandalonePluginHolder> pluginHolder;
};

} // namespace juce

//==============================================================================
// This creates the application instance
// Note: JUCE's juce_audio_plugin_client_Standalone.cpp will provide the main entry point
JUCE_CREATE_APPLICATION_DEFINE(juce::CustomStandaloneFilterApp)

#endif // JucePlugin_Build_Standalone
