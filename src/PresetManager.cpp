#include "PresetManager.h"
#include "PluginProcessor.h"
#include "PluginConstants.h"

#include <jsfx.h>

extern jsfxAPI JesusonicAPI;

//==============================================================================
PresetManager::PresetManager(AudioPluginAudioProcessor& proc)
    : processor(proc)
{
    initializeProperties();
}

PresetManager::~PresetManager() = default;

//==============================================================================
void PresetManager::initializeProperties()
{
    juce::PropertiesFile::Options options;
    options.applicationName = JucePlugin_Name;
    options.filenameSuffix = ".properties";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = JucePlugin_Name;
    options.commonToAllUsers = false;

    appProperties.setStorageParameters(options);
}

juce::File PresetManager::getPresetRootDirectory() const
{
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    return appDataDir.getChildFile(JucePlugin_Name);
}

juce::File PresetManager::getJsfxStorageDirectory() const
{
    auto jsfxPath = processor.getCurrentJSFXPath();
    if (jsfxPath.isEmpty())
        return {};

    // Get author name from processor
    juce::String author = processor.getCurrentJSFXAuthor();
    if (author.isEmpty())
        author = "Unknown";

    // Get JSFX name
    juce::File jsfxFile(jsfxPath);
    juce::String jsfxName = sanitizeFilename(jsfxFile.getFileNameWithoutExtension());

    // Create path: <appdata>/data/author-name/jsfx-name
    auto storageDir =
        getPresetRootDirectory().getChildFile("data").getChildFile(sanitizeFilename(author)).getChildFile(jsfxName);

    // Create directory if it doesn't exist
    if (!storageDir.exists())
        storageDir.createDirectory();

    return storageDir;
}

juce::File PresetManager::getDefaultPresetFile() const
{
    auto storageDir = getJsfxStorageDirectory();
    if (storageDir == juce::File())
        return {};

    return storageDir.getChildFile("default.rpl");
}

bool PresetManager::hasDefaultPreset() const
{
    return getDefaultPresetFile().existsAsFile();
}

juce::String PresetManager::sanitizeFilename(const juce::String& name) const
{
    // Replace invalid filename characters with underscores
    juce::String result = name;
    juce::String invalidChars = "<>:\"/\\|?*";

    for (int i = 0; i < invalidChars.length(); ++i)
        result = result.replaceCharacter(invalidChars[i], '_');

    return result;
}

//==============================================================================
juce::String PresetManager::getCurrentPresetAsBase64() const
{
    auto* instance = processor.getSXInstancePtr();
    if (!instance)
        return {};

    // Get parameter count
    int numParams = JesusonicAPI.sx_getNumParms(instance);
    if (numParams <= 0)
        return {};

    // Build preset data string (same format as JSFX uses)
    juce::StringArray values;

    for (int i = 0; i < numParams; ++i)
    {
        double minVal = 0.0, maxVal = 1.0, step = 0.0;
        double value = JesusonicAPI.sx_getParmVal(instance, i, &minVal, &maxVal, &step);
        values.add(juce::String(value, 6)); // 6 decimal places like REAPER does
    }

    juce::String presetText = values.joinIntoString(" "); // Space-separated, not newline

    // Encode to base64
    juce::MemoryOutputStream outStream;
    juce::Base64::convertToBase64(outStream, presetText.toRawUTF8(), presetText.getNumBytesAsUTF8());

    return outStream.toString();
}

bool PresetManager::savePresetToFile(
    const juce::File& file,
    const juce::String& bankName,
    const juce::String& presetName,
    const juce::String& base64Data
)
{
    if (base64Data.isEmpty())
    {
        DBG("PresetManager::savePresetToFile - Empty preset data");
        return false;
    }

    // Build .rpl file content
    juce::String content;
    content << "<REAPER_PRESET_LIBRARY `" << bankName << "`\n";
    content << "  <PRESET `" << presetName << "`\n";
    content << "    " << base64Data << "\n";
    content << "  >\n";
    content << ">\n";

    // Write to file
    if (file.replaceWithText(content))
    {
        DBG("PresetManager::savePresetToFile - Saved to: " << file.getFullPathName());
        return true;
    }

    DBG("PresetManager::savePresetToFile - Failed to write file: " << file.getFullPathName());
    return false;
}

//==============================================================================
void PresetManager::resetToDefault(juce::Component* parentComponent)
{
    auto* instance = processor.getSXInstancePtr();
    if (!instance)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "No JSFX Loaded",
            "Please load a JSFX file first."
        );
        return;
    }

    // Show confirmation dialog
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        "Reset to Default",
        "This will reset all parameters to their default values. Continue?",
        "Reset",
        "Cancel",
        parentComponent,
        juce::ModalCallbackFunction::create(
            [this, instance](int result)
            {
                if (result == 1) // OK was clicked
                {
                    // First, try to load the default preset if it exists
                    if (applyDefaultPresetIfExists())
                    {
                        DBG("PresetManager: Reset loaded default preset");
                        return;
                    }

                    // If no default preset exists, reset to JSFX slider defaults
                    // Reset all parameters to their slider default values
                    // The JSFX m_sliders array contains default_val for each parameter
                    int numParams = JesusonicAPI.sx_getNumParms(instance);
                    for (int i = 0; i < numParams; ++i)
                    {
                        // Get the slider's default value from the m_sliders array
                        double defaultValue = 0.0;
                        if (i < instance->m_sliders.GetSize())
                        {
                            auto* slider = instance->m_sliders.Get(i);
                            if (slider)
                                defaultValue = slider->default_val;
                        }

                        // Set parameter value in JSFX
                        JesusonicAPI.sx_setParmVal(instance, i, defaultValue, 0);

                        // Update APVTS with normalized value
                        auto paramID = juce::String("param") + juce::String(i);
                        if (auto* param = processor.getAPVTS().getParameter(paramID))
                        {
                            // Get parameter range for normalization
                            double minVal = 0.0, maxVal = 1.0, step = 0.0;
                            JesusonicAPI.sx_getParmVal(instance, i, &minVal, &maxVal, &step);

                            // Convert JSFX value to normalized [0, 1]
                            float normalizedValue = (maxVal != minVal)
                                                      ? static_cast<float>((defaultValue - minVal) / (maxVal - minVal))
                                                      : 0.0f;
                            normalizedValue = juce::jlimit(0.0f, 1.0f, normalizedValue);

                            param->setValueNotifyingHost(normalizedValue);
                        }
                    }

                    DBG("PresetManager: Reset to JSFX slider default values");
                }
            }
        )
    );
}

void PresetManager::saveAs(juce::Component* parentComponent)
{
    auto* instance = processor.getSXInstancePtr();
    if (!instance)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "No JSFX Loaded",
            "Please load a JSFX file first."
        );
        return;
    }

    // Create dialog to get bank and preset names
    auto* alertWindow = new juce::AlertWindow(
        "Save Preset",
        "Enter bank and preset names:",
        juce::MessageBoxIconType::NoIcon,
        parentComponent
    );

    alertWindow->addTextEditor("bank", "User", "Bank name:");
    alertWindow->addTextEditor("preset", "", "Preset name:");

    alertWindow->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alertWindow->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [this, alertWindow](int result)
            {
                if (result == 1) // Save was clicked
                {
                    juce::String bankName = alertWindow->getTextEditorContents("bank").trim();
                    juce::String presetName = alertWindow->getTextEditorContents("preset").trim();

                    if (presetName.isEmpty())
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Invalid Name",
                            "Please enter a preset name."
                        );
                        return;
                    }

                    if (bankName.isEmpty())
                        bankName = "User";

                    // Get current preset data
                    juce::String base64Data = getCurrentPresetAsBase64();
                    if (base64Data.isEmpty())
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Error",
                            "Failed to capture preset data."
                        );
                        return;
                    }

                    // Save to storage directory
                    juce::String filename = sanitizeFilename(bankName) + ".rpl";
                    juce::File presetFile = getJsfxStorageDirectory().getChildFile(filename);

                    if (savePresetToFile(presetFile, bankName, presetName, base64Data))
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::InfoIcon,
                            "Success",
                            "Preset saved successfully!"
                        );

                        // Notify that presets have changed
                        if (onPresetsChanged)
                            onPresetsChanged();
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Error",
                            "Failed to save preset."
                        );
                    }
                }

                delete alertWindow;
            }
        ),
        true
    );
}

void PresetManager::setAsDefault(juce::Component* parentComponent)
{
    auto* instance = processor.getSXInstancePtr();
    if (!instance)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "No JSFX Loaded",
            "Please load a JSFX file first."
        );
        return;
    }

    // Show confirmation dialog
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        "Set as Default",
        "This will save the current state as the default preset for this JSFX. "
        "It will be automatically loaded whenever this JSFX is loaded. Continue?",
        "Set as Default",
        "Cancel",
        parentComponent,
        juce::ModalCallbackFunction::create(
            [this](int result)
            {
                if (result == 1) // OK was clicked
                {
                    // Get current preset data
                    juce::String base64Data = getCurrentPresetAsBase64();
                    if (base64Data.isEmpty())
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Error",
                            "Failed to capture preset data."
                        );
                        return;
                    }

                    // Save to default preset file
                    juce::File defaultFile = getDefaultPresetFile();
                    juce::String jsfxName = processor.getCurrentJSFXName();

                    if (savePresetToFile(defaultFile, jsfxName, "Default", base64Data))
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::InfoIcon,
                            "Success",
                            "Default preset saved successfully!"
                        );
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Error",
                            "Failed to save default preset."
                        );
                    }
                }
            }
        )
    );
}

void PresetManager::importPreset(juce::Component* parentComponent)
{
    if (processor.getCurrentJSFXPath().isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "No JSFX Loaded",
            "Please load a JSFX file first."
        );
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        "Import Preset File",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rpl"
    );

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(
        flags,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto selectedFile = fc.getResult();
            if (selectedFile == juce::File())
                return; // User cancelled

            // Copy file to JSFX storage directory
            juce::File destFile = getJsfxStorageDirectory().getChildFile(selectedFile.getFileName());

            if (destFile.existsAsFile())
            {
                // File already exists - ask to overwrite
                juce::AlertWindow::showOkCancelBox(
                    juce::MessageBoxIconType::QuestionIcon,
                    "File Exists",
                    "A preset with this name already exists. Overwrite?",
                    "Overwrite",
                    "Cancel",
                    nullptr,
                    juce::ModalCallbackFunction::create(
                        [this, selectedFile, destFile](int result)
                        {
                            if (result == 1) // Overwrite
                            {
                                if (selectedFile.copyFileTo(destFile))
                                {
                                    juce::AlertWindow::showMessageBoxAsync(
                                        juce::MessageBoxIconType::InfoIcon,
                                        "Success",
                                        "Preset imported successfully!"
                                    );

                                    // Notify that presets have changed
                                    if (onPresetsChanged)
                                        onPresetsChanged();
                                }
                                else
                                {
                                    juce::AlertWindow::showMessageBoxAsync(
                                        juce::MessageBoxIconType::WarningIcon,
                                        "Error",
                                        "Failed to import preset."
                                    );
                                }
                            }
                        }
                    )
                );
            }
            else
            {
                // No conflict - copy directly
                if (selectedFile.copyFileTo(destFile))
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        "Success",
                        "Preset imported successfully!"
                    );

                    // Notify that presets have changed
                    if (onPresetsChanged)
                        onPresetsChanged();
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Error",
                        "Failed to import preset."
                    );
                }
            }
        }
    );
}

void PresetManager::exportPreset(juce::Component* parentComponent)
{
    auto* instance = processor.getSXInstancePtr();
    if (!instance)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "No JSFX Loaded",
            "Please load a JSFX file first."
        );
        return;
    }

    // Create dialog to choose export type
    auto* alertWindow = new juce::AlertWindow(
        "Export Preset",
        "Choose what to export:",
        juce::MessageBoxIconType::NoIcon,
        parentComponent
    );

    alertWindow->addButton("Export All", 1);
    alertWindow->addButton("Export Bank", 2);
    alertWindow->addButton("Export Preset", 3);
    alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alertWindow->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [this, alertWindow, parentComponent](int result)
            {
                delete alertWindow;

                if (result == 0) // Cancel
                    return;

                if (result == 1) // Export All
                    exportAllPresets(parentComponent);
                else if (result == 2) // Export Bank
                    exportBankDialog(parentComponent);
                else if (result == 3) // Export Preset
                    exportPresetDialog(parentComponent);
            }
        ),
        true
    );
}

void PresetManager::exportAllPresets(juce::Component* parentComponent)
{
    juce::String jsfxName = processor.getCurrentJSFXName();
    if (jsfxName.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Error", "No JSFX loaded.");
        return;
    }

    // Collect all .rpl files from all sources (same logic as updatePresetList in PluginEditor)
    juce::Array<juce::File> allFiles;

    // 1. JSFX directory (where the .jsfx file is located)
    auto currentJsfxPath = processor.getCurrentJSFXPath();
    if (!currentJsfxPath.isEmpty())
    {
        juce::File jsfxFile(currentJsfxPath);
        juce::File jsfxDirectory = jsfxFile.getParentDirectory();
        if (jsfxDirectory.exists())
        {
            auto localRplFiles = jsfxDirectory.findChildFiles(juce::File::findFiles, false, "*.rpl");
            for (const auto& file : localRplFiles)
                allFiles.add(file);
        }
    }

    // 2. AppData storage directory (user-saved/imported presets)
    juce::File storageDir = getJsfxStorageDirectory();
    if (storageDir.exists() && storageDir.isDirectory())
    {
        auto storedPresets = storageDir.findChildFiles(juce::File::findFiles, false, "*.rpl");
        for (const auto& file : storedPresets)
        {
            // Skip default.rpl to avoid duplication
            if (file.getFileName() != "default.rpl")
                allFiles.add(file);
        }
    }

    // 3. REAPER Effects directory (matching JSFX name)
    auto reaperEffectsPath = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                 .getChildFile("REAPER")
                                 .getChildFile("Effects");
    if (reaperEffectsPath.exists())
    {
        auto rplFiles = reaperEffectsPath.findChildFiles(juce::File::findFiles, true, "*.rpl");
        for (const auto& file : rplFiles)
        {
            juce::String filename = file.getFileNameWithoutExtension();
            if (filename.equalsIgnoreCase(jsfxName))
                allFiles.add(file);
        }
    }

    if (allFiles.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Error",
            "No presets found to export."
        );
        return;
    }

    // Show file chooser to save combined preset file
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export All Presets",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rpl"
    );

    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, allFiles, jsfxName](const juce::FileChooser& fc)
        {
            auto selectedFile = fc.getResult();
            if (selectedFile == juce::File())
                return; // User cancelled

            // Ensure .rpl extension
            if (!selectedFile.hasFileExtension(".rpl"))
                selectedFile = selectedFile.withFileExtension(".rpl");

            // Combine all preset files into one
            if (combinePresetFiles(allFiles, selectedFile))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    "Success",
                    "All presets exported successfully!"
                );
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Error",
                    "Failed to export presets."
                );
            }
        }
    );
}

void PresetManager::exportBankDialog(juce::Component* parentComponent)
{
    juce::String jsfxName = processor.getCurrentJSFXName();
    if (jsfxName.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Error", "No JSFX loaded.");
        return;
    }

    // Collect all .rpl files from all sources (same logic as exportAllPresets)
    juce::Array<juce::File> allFiles;

    // 1. JSFX directory
    auto currentJsfxPath = processor.getCurrentJSFXPath();
    if (!currentJsfxPath.isEmpty())
    {
        juce::File jsfxFile(currentJsfxPath);
        juce::File jsfxDirectory = jsfxFile.getParentDirectory();
        if (jsfxDirectory.exists())
        {
            auto localRplFiles = jsfxDirectory.findChildFiles(juce::File::findFiles, false, "*.rpl");
            for (const auto& file : localRplFiles)
                allFiles.add(file);
        }
    }

    // 2. AppData storage directory
    juce::File storageDir = getJsfxStorageDirectory();
    if (storageDir.exists() && storageDir.isDirectory())
    {
        auto storedPresets = storageDir.findChildFiles(juce::File::findFiles, false, "*.rpl");
        for (const auto& file : storedPresets)
            if (file.getFileName() != "default.rpl")
                allFiles.add(file);
    }

    // 3. REAPER Effects directory
    auto reaperEffectsPath = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                 .getChildFile("REAPER")
                                 .getChildFile("Effects");
    if (reaperEffectsPath.exists())
    {
        auto rplFiles = reaperEffectsPath.findChildFiles(juce::File::findFiles, true, "*.rpl");
        for (const auto& file : rplFiles)
        {
            juce::String filename = file.getFileNameWithoutExtension();
            if (filename.equalsIgnoreCase(jsfxName))
                allFiles.add(file);
        }
    }

    if (allFiles.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Error",
            "No banks found to export."
        );
        return;
    }

    // Create dialog to choose bank
    auto* alertWindow = new juce::AlertWindow(
        "Export Bank",
        "Choose bank to export:",
        juce::MessageBoxIconType::NoIcon,
        parentComponent
    );

    juce::StringArray bankNames;
    for (const auto& file : allFiles)
        bankNames.add(file.getFileNameWithoutExtension());

    alertWindow->addComboBox("bank", bankNames, "Bank:");
    alertWindow->addButton("Export", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alertWindow->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [this, alertWindow, allFiles](int result)
            {
                if (result == 1) // Export
                {
                    int selectedIndex = alertWindow->getComboBoxComponent("bank")->getSelectedItemIndex();
                    if (selectedIndex >= 0 && selectedIndex < allFiles.size())
                    {
                        juce::File bankFile = allFiles[selectedIndex];

                        // Show file chooser
                        auto chooser = std::make_shared<juce::FileChooser>(
                            "Export Bank",
                            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                            "*.rpl"
                        );

                        chooser->launchAsync(
                            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                            [bankFile, chooser](const juce::FileChooser& fc)
                            {
                                auto selectedFile = fc.getResult();
                                if (selectedFile == juce::File())
                                    return;

                                if (!selectedFile.hasFileExtension(".rpl"))
                                    selectedFile = selectedFile.withFileExtension(".rpl");

                                if (bankFile.copyFileTo(selectedFile))
                                {
                                    juce::AlertWindow::showMessageBoxAsync(
                                        juce::MessageBoxIconType::InfoIcon,
                                        "Success",
                                        "Bank exported successfully!"
                                    );
                                }
                                else
                                {
                                    juce::AlertWindow::showMessageBoxAsync(
                                        juce::MessageBoxIconType::WarningIcon,
                                        "Error",
                                        "Failed to export bank."
                                    );
                                }
                            }
                        );
                    }
                }
                delete alertWindow;
            }
        ),
        true
    );
}

void PresetManager::exportPresetDialog(juce::Component* parentComponent)
{
    auto* instance = processor.getSXInstancePtr();
    if (!instance)
        return;

    // Create dialog to get bank and preset names
    auto* alertWindow = new juce::AlertWindow(
        "Export Preset",
        "Enter bank and preset names:",
        juce::MessageBoxIconType::NoIcon,
        parentComponent
    );

    alertWindow->addTextEditor("bank", "User", "Bank name:");
    alertWindow->addTextEditor("preset", "", "Preset name:");
    alertWindow->addButton("Export", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alertWindow->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [this, alertWindow](int result)
            {
                if (result == 1) // Export
                {
                    juce::String bankName = alertWindow->getTextEditorContents("bank").trim();
                    juce::String presetName = alertWindow->getTextEditorContents("preset").trim();

                    if (presetName.isEmpty())
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Invalid Name",
                            "Please enter a preset name."
                        );
                        delete alertWindow;
                        return;
                    }

                    if (bankName.isEmpty())
                        bankName = "User";

                    // Get current preset data
                    juce::String base64Data = getCurrentPresetAsBase64();
                    if (base64Data.isEmpty())
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Error",
                            "Failed to capture preset data."
                        );
                        delete alertWindow;
                        return;
                    }

                    // Show file chooser
                    auto chooser = std::make_shared<juce::FileChooser>(
                        "Export Preset",
                        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                        "*.rpl"
                    );

                    chooser->launchAsync(
                        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                        [this, chooser, bankName, presetName, base64Data](const juce::FileChooser& fc)
                        {
                            auto selectedFile = fc.getResult();
                            if (selectedFile == juce::File())
                                return;

                            if (!selectedFile.hasFileExtension(".rpl"))
                                selectedFile = selectedFile.withFileExtension(".rpl");

                            if (savePresetToFile(selectedFile, bankName, presetName, base64Data))
                            {
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::MessageBoxIconType::InfoIcon,
                                    "Success",
                                    "Preset exported successfully!"
                                );
                            }
                            else
                            {
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::MessageBoxIconType::WarningIcon,
                                    "Error",
                                    "Failed to export preset."
                                );
                            }
                        }
                    );
                }
                delete alertWindow;
            }
        ),
        true
    );
}

void PresetManager::deletePreset(
    juce::Component* parentComponent,
    const juce::String& bankName,
    const juce::String& presetName
)
{
    if (presetName.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Error", "No preset selected.");
        return;
    }

    // Show confirmation dialog with bank and preset info
    juce::String message = "Delete preset:\n\n";
    message += "Bank: " + bankName + "\n";
    message += "Preset: " + presetName + "\n\n";
    message += "This will search all preset files and delete the first match.";

    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        "Delete Preset",
        message,
        "Delete",
        "Cancel",
        parentComponent,
        juce::ModalCallbackFunction::create(
            [this, bankName, presetName](int result)
            {
                if (result == 1) // Delete was clicked
                {
                    juce::String jsfxName = processor.getCurrentJSFXName();
                    if (jsfxName.isEmpty())
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Error",
                            "No JSFX loaded."
                        );
                        return;
                    }

                    // Collect all .rpl files from all sources (same as export)
                    juce::Array<juce::File> allFiles;

                    // 1. JSFX directory
                    auto currentJsfxPath = processor.getCurrentJSFXPath();
                    if (!currentJsfxPath.isEmpty())
                    {
                        juce::File jsfxFile(currentJsfxPath);
                        juce::File jsfxDirectory = jsfxFile.getParentDirectory();
                        if (jsfxDirectory.exists())
                        {
                            auto localRplFiles = jsfxDirectory.findChildFiles(juce::File::findFiles, false, "*.rpl");
                            for (const auto& file : localRplFiles)
                                allFiles.add(file);
                        }
                    }

                    // 2. AppData storage directory
                    juce::File storageDir = getJsfxStorageDirectory();
                    if (storageDir.exists() && storageDir.isDirectory())
                    {
                        auto storedPresets = storageDir.findChildFiles(juce::File::findFiles, false, "*.rpl");
                        for (const auto& file : storedPresets)
                            allFiles.add(file);
                    }

                    // 3. REAPER Effects directory
                    auto reaperEffectsPath = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                                 .getChildFile("REAPER")
                                                 .getChildFile("Effects");
                    if (reaperEffectsPath.exists())
                    {
                        auto rplFiles = reaperEffectsPath.findChildFiles(juce::File::findFiles, true, "*.rpl");
                        for (const auto& file : rplFiles)
                        {
                            juce::String filename = file.getFileNameWithoutExtension();
                            if (filename.equalsIgnoreCase(jsfxName))
                                allFiles.add(file);
                        }
                    }

                    // Search through all files and delete from the first match
                    bool presetDeleted = false;
                    juce::String deletedFromFile;

                    for (const auto& file : allFiles)
                    {
                        // Read file content
                        juce::String content = file.loadFileAsString();
                        if (content.isEmpty())
                            continue;

                        // Try to remove the preset
                        if (removePresetFromContent(content, bankName, presetName))
                        {
                            // Save the modified content back
                            if (file.replaceWithText(content))
                            {
                                presetDeleted = true;
                                deletedFromFile = file.getFullPathName();
                                break; // Stop after first match
                            }
                        }
                    }

                    if (presetDeleted)
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::InfoIcon,
                            "Success",
                            "Preset '" + presetName + "' deleted from:\n" + deletedFromFile
                        );

                        // Notify that presets have changed
                        if (onPresetsChanged)
                            onPresetsChanged();
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Error",
                            "Preset not found in any file."
                        );
                    }
                }
            }
        )
    );
}

bool PresetManager::applyDefaultPresetIfExists()
{
    juce::File defaultFile = getDefaultPresetFile();

    if (!defaultFile.existsAsFile())
    {
        DBG("PresetManager: No default preset file found at: " << defaultFile.getFullPathName().toRawUTF8());
        return false;
    }

    DBG("PresetManager: Found default preset file: " << defaultFile.getFullPathName().toRawUTF8());

    // Read the preset file
    auto content = defaultFile.loadFileAsString();
    if (content.isEmpty())
    {
        DBG("PresetManager: Default preset file is empty");
        return false;
    }

    DBG("PresetManager: File content length: " << content.length());
    DBG("PresetManager: File content (first 200 chars): " << content.substring(0, 200).toRawUTF8());

    // Parse the .rpl file to extract base64 data
    // Format: <REAPER_PRESET_LIBRARY `name`
    //           <PRESET `name`
    //             BASE64_DATA
    //           >
    //         >

    int presetStart = content.indexOf("<PRESET");
    if (presetStart == -1)
    {
        DBG("PresetManager: Invalid preset file format (no <PRESET tag)");
        return false;
    }

    DBG("PresetManager: Found <PRESET tag at position: " << presetStart);

    // Find the line ending after <PRESET tag (after the backtick and newline)
    int lineEnd = content.indexOfChar(presetStart, '\n');
    if (lineEnd == -1)
    {
        DBG("PresetManager: No newline found after <PRESET tag");
        return false;
    }

    int dataStart = lineEnd + 1; // Start after the newline
    DBG("PresetManager: Data starts at position: " << dataStart);

    // Find the closing > that's at the start of a line
    // Look for pattern: newline followed by optional spaces, then >
    int dataEnd = dataStart;
    while (dataEnd < content.length())
    {
        int nextNewline = content.indexOfChar(dataEnd, '\n');
        if (nextNewline == -1)
            break;

        // Check if the line after this newline starts with spaces and >
        int checkPos = nextNewline + 1;
        while (checkPos < content.length() && (content[checkPos] == ' ' || content[checkPos] == '\t'))
            checkPos++;

        if (checkPos < content.length() && content[checkPos] == '>')
        {
            dataEnd = nextNewline; // End before the newline
            break;
        }

        dataEnd = nextNewline + 1;
    }

    DBG("PresetManager: Data ends at position: " << dataEnd);

    // Extract base64 data between the tags
    juce::String base64Data = content.substring(dataStart, dataEnd).trim();

    if (base64Data.isEmpty())
    {
        DBG("PresetManager: No preset data found in file");
        return false;
    }

    DBG("PresetManager: Extracted base64 data length: " << base64Data.length());
    DBG("PresetManager: Base64 data (first 100 chars): " << base64Data.substring(0, 100).toRawUTF8());

    // Load the preset using the processor's method
    if (processor.loadPresetFromBase64(base64Data))
    {
        DBG("PresetManager: Default preset applied successfully");
        return true;
    }

    DBG("PresetManager: Failed to apply default preset");
    return false;
}

bool PresetManager::removePresetFromContent(
    juce::String& content,
    const juce::String& bankName,
    const juce::String& presetName
)
{
    // Find the bank (library) with the given name
    juce::String bankTag = "<REAPER_PRESET_LIBRARY `" + bankName + "`";
    int bankStart = content.indexOf(bankTag);

    if (bankStart == -1)
    {
        DBG("PresetManager::removePresetFromContent - Bank not found: " << bankName);
        return false;
    }

    // Find the end of this bank (closing >)
    int bankEnd = bankStart;
    int depth = 1;
    const char* data = content.toRawUTF8();
    int len = content.length();

    // Skip past the opening tag
    int searchPos = bankStart + bankTag.length();
    while (searchPos < len && data[searchPos] != '\n')
        searchPos++;

    for (int i = searchPos; i < len && depth > 0; i++)
    {
        char c = data[i];
        // Skip quoted sections
        if (c == '`' || c == '"' || c == '\'')
        {
            char quote = c;
            i++;
            while (i < len && data[i] != quote)
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

    if (bankEnd == bankStart)
    {
        DBG("PresetManager::removePresetFromContent - Could not find end of bank");
        return false;
    }

    // Now find the preset within this bank
    juce::String presetTag = "<PRESET `" + presetName + "`";
    int presetStart = content.indexOf(bankStart, presetTag);

    // Make sure we found it within this bank, not in another bank
    if (presetStart == -1 || presetStart > bankEnd)
    {
        DBG("PresetManager::removePresetFromContent - Preset not found in bank: " << presetName);
        return false;
    }

    // Find the end of this preset (closing >)
    int presetEnd = presetStart;
    depth = 1;
    searchPos = presetStart + presetTag.length();

    // Skip past the opening tag line
    while (searchPos < bankEnd && data[searchPos] != '\n')
        searchPos++;

    for (int i = searchPos; i < bankEnd && depth > 0; i++)
    {
        char c = data[i];
        // Skip quoted sections
        if (c == '`' || c == '"' || c == '\'')
        {
            char quote = c;
            i++;
            while (i < bankEnd && data[i] != quote)
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
                presetEnd = i + 1; // Include the closing >
                break;
            }
        }
    }

    if (presetEnd == presetStart)
    {
        DBG("PresetManager::removePresetFromContent - Could not find end of preset");
        return false;
    }

    // Find the start of the line containing <PRESET (to include indentation)
    int lineStart = presetStart;
    while (lineStart > 0 && data[lineStart - 1] != '\n')
        lineStart--;

    // Find the end of the line containing the closing > (to include newline)
    int lineEnd = presetEnd;
    while (lineEnd < len && data[lineEnd] != '\n')
        lineEnd++;
    if (lineEnd < len)
        lineEnd++; // Include the newline

    // Remove the preset block including its line breaks
    content = content.substring(0, lineStart) + content.substring(lineEnd);

    DBG("PresetManager::removePresetFromContent - Removed preset: " << presetName << " from bank: " << bankName);
    return true;
}

bool PresetManager::combinePresetFiles(const juce::Array<juce::File>& files, const juce::File& outputFile)
{
    if (files.isEmpty())
        return false;

    // Build combined content with all presets from all banks
    juce::String jsfxName = processor.getCurrentJSFXName();
    juce::String combinedContent;
    combinedContent << "<REAPER_PRESET_LIBRARY `" << jsfxName << " - All Presets`\n";

    // Read and combine all preset files (each file is a bank with presets)
    for (const auto& file : files)
    {
        juce::String content = file.loadFileAsString();

        // Find all <PRESET> blocks in this file and add them
        int searchPos = 0;
        while (searchPos < content.length())
        {
            int presetStart = content.indexOf(searchPos, "<PRESET");
            if (presetStart == -1)
                break;

            // Find the closing > for this preset
            int pos = presetStart;
            int presetEnd = -1;

            // Skip past the <PRESET `name` line
            int nameLineEnd = content.indexOfChar(pos, '\n');
            if (nameLineEnd == -1)
                break;

            pos = nameLineEnd + 1;

            // Find the closing > at the start of a line (with optional leading spaces)
            while (pos < content.length())
            {
                int nextNewline = content.indexOfChar(pos, '\n');
                if (nextNewline == -1)
                {
                    // Check if we're at the end and there's a > here
                    int checkPos = pos;
                    while (checkPos < content.length() && (content[checkPos] == ' ' || content[checkPos] == '\t'))
                        checkPos++;
                    if (checkPos < content.length() && content[checkPos] == '>')
                    {
                        presetEnd = checkPos + 1;
                        break;
                    }
                    break;
                }

                int checkPos = nextNewline + 1;
                while (checkPos < content.length() && (content[checkPos] == ' ' || content[checkPos] == '\t'))
                    checkPos++;

                if (checkPos < content.length() && content[checkPos] == '>')
                {
                    presetEnd = checkPos + 1;
                    break;
                }

                pos = nextNewline + 1;
            }

            if (presetEnd == -1)
                break;

            // Extract the entire <PRESET>...> block including indentation
            juce::String presetBlock = content.substring(presetStart, presetEnd);
            combinedContent += "  " + presetBlock + "\n";

            searchPos = presetEnd;
        }
    }

    combinedContent += ">\n";

    // Write to output file
    return outputFile.replaceWithText(combinedContent);
}
