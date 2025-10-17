#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

struct SX_Instance;

/**
 * JSFX IDE Editor Window Manager
 * Creates and manages the classic dialog-based JSFX editor as a native window
 */
class JsfxEditorWindow
{
public:
    JsfxEditorWindow();
    ~JsfxEditorWindow();

    // Open the editor for the given JSFX instance
    // parentComponent is used to get the parent window handle on Windows/macOS
    void open(SX_Instance* instance, juce::Component* parentComponent);

    // Close the editor window
    void close();

    // Check if editor is currently open
    bool isOpen() const;

    // Bring editor to front if open
    void bringToFront();

private:
    SX_Instance* currentInstance = nullptr;
    void* editorWindowHandle = nullptr; // HWND

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsfxEditorWindow)
};
