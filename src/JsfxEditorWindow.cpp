#include "JsfxEditorWindow.h"

#include "../jsfx/include/jsfx.h"

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#ifdef __linux__
// Linux uses SWELL for dialog creation
#include <WDL/swell/swell-dlggen.h>
#include <WDL/swell/swell.h>

extern struct SWELL_DialogResourceIndex* SWELL_curmodule_dialogresource_head;
extern "C" INT_PTR _ZN11SX_Instance13_watchDlgProcEP6HWND__jml(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#elif defined(__APPLE__)
// macOS uses native CreateDialogParam
#include <WDL/swell/swell.h>

extern "C" INT_PTR _ZN11SX_Instance13_watchDlgProcEP6HWND__jml(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#else
// Windows uses native CreateDialogParam
#include <WDL/swell/swell.h>
#include "../jsfx/jsfx/jsfx/sfxui.h"

#endif

extern HINSTANCE g_hInst;

// Resource ID for the JSFX debug/editor dialog
#define IDD_JSDEBUG 114

JsfxEditorWindow::JsfxEditorWindow()
    : editorWindowHandle(nullptr)
    , currentInstance(nullptr)
{
}

JsfxEditorWindow::~JsfxEditorWindow()
{
    close();
}

void JsfxEditorWindow::open(SX_Instance* instance, juce::Component* parentComponent)
{
    if (!instance)
        return;

    // If editor already open for this instance, bring it to front
    if (editorWindowHandle && currentInstance == instance)
    {
        bringToFront();
        return;
    }

    // Close any existing editor first
    close();

    currentInstance = instance;

#ifdef __linux__
    // Linux: Use SWELL_CreateDialog
    if (!SWELL_curmodule_dialogresource_head)
        return;

    auto dlgproc = (DLGPROC)_ZN11SX_Instance13_watchDlgProcEP6HWND__jml;
    editorWindowHandle = SWELL_CreateDialog(
        SWELL_curmodule_dialogresource_head,
        MAKEINTRESOURCE(IDD_JSDEBUG),
        nullptr,
        dlgproc,
        (LPARAM)instance
    );

#elif defined(__APPLE__)
    // macOS: Use CreateDialogParam with JUCE window as parent
    HWND parentHwnd = parentComponent ? (HWND)parentComponent->getWindowHandle() : nullptr;
    auto dlgproc = (DLGPROC)_ZN11SX_Instance13_watchDlgProcEP6HWND__jml;

    editorWindowHandle =
        CreateDialogParam(g_hInst, MAKEINTRESOURCE(IDD_JSDEBUG), parentHwnd, dlgproc, (LPARAM)instance);

#else
    // Windows: Use CreateDialogParam with JUCE window as parent
    HWND parentHwnd = parentComponent ? (HWND)parentComponent->getWindowHandle() : nullptr;

    editorWindowHandle = CreateDialogParam(
        g_hInst,
        MAKEINTRESOURCE(IDD_JSDEBUG),
        parentHwnd,
        SX_Instance::_watchDlgProc,
        (LPARAM)instance
    );
#endif

    if (editorWindowHandle)
    {
        instance->m_hwndwatch = (HWND)editorWindowHandle;
        ShowWindow((HWND)editorWindowHandle, SW_SHOW);
    }
    else
    {
        currentInstance = nullptr;
    }
}

void JsfxEditorWindow::close()
{
    if (editorWindowHandle)
    {
        // Clear reference in JSFX instance
        if (currentInstance && currentInstance->m_hwndwatch == (HWND)editorWindowHandle)
            currentInstance->m_hwndwatch = nullptr;

        // Destroy the window
        DestroyWindow((HWND)editorWindowHandle);
        editorWindowHandle = nullptr;
        currentInstance = nullptr;
    }
}

bool JsfxEditorWindow::isOpen() const
{
    if (editorWindowHandle && !IsWindow((HWND)editorWindowHandle))
    {
        // Window was closed externally (e.g., by clicking X), clean up our state
        if (currentInstance && currentInstance->m_hwndwatch == (HWND)editorWindowHandle)
            currentInstance->m_hwndwatch = nullptr;

        // Cast away const to update state (this is safe since we're just cleaning up invalid state)
        const_cast<JsfxEditorWindow*>(this)->editorWindowHandle = nullptr;
        const_cast<JsfxEditorWindow*>(this)->currentInstance = nullptr;
        return false;
    }

    return editorWindowHandle != nullptr;
}

void JsfxEditorWindow::bringToFront()
{
    if (isOpen())
        SetForegroundWindow((HWND)editorWindowHandle);
}
