#pragma once

#include <juce_core/juce_core.h>

struct SX_Instance;

/**
 * JSFX IDE Editor Window Manager
 * Creates and manages the classic ncurses-based JSFX editor as a native floating window
 */
class JsfxEditorWindow
{
public:
    JsfxEditorWindow();
    ~JsfxEditorWindow();

    // Open the editor for the given JSFX instance
    void open(SX_Instance* instance);

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
