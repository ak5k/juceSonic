#include "JsfxEditorWindow.h"

#include "../jsfx/include/jsfx.h"

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Define DBG macro if not already defined
#ifndef DBG
#define DBG(x) juce::Logger::writeToLog(x)
#endif

#ifndef _WIN32
#include <WDL/swell/swell-dlggen.h>
#include <WDL/swell/swell.h>

// Forward declare the JSFX module's dialog resource head
// This is the single instance defined in jsfx_api.cpp
// There should be only one in the final binary
extern struct SWELL_DialogResourceIndex* SWELL_curmodule_dialogresource_head;

// Declare SX_Instance::_watchDlgProc using its mangled name
// This is a static member function defined in sfx_edit.cpp
extern "C" INT_PTR _ZN11SX_Instance13_watchDlgProcEP6HWND__jml(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif

// External SWELL instance (defined in jsfx_api.cpp)
extern HINSTANCE g_hInst;

// Resource ID for the JSFX debug/editor dialog (from resource.h)
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

void JsfxEditorWindow::open(SX_Instance* instance)
{
    if (!instance)
    {
        DBG("JsfxEditorWindow::open() - No instance provided");
        return;
    }

    // If editor already open for this instance, just bring it to front
    if (editorWindowHandle && currentInstance == instance)
    {
        DBG("JsfxEditorWindow::open() - Editor already open, bringing to front");
        bringToFront();
        return;
    }

    // Close any existing editor first
    close();

    currentInstance = instance;

#ifndef _WIN32
    // Create the JSFX editor dialog using SWELL
    HWND parentHwnd = nullptr;

    DBG("JsfxEditorWindow::open() - Creating JSFX editor dialog...");
    DBG("JsfxEditorWindow::open() - SWELL_curmodule_dialogresource_head = "
        + juce::String::toHexString((juce::pointer_sized_int)SWELL_curmodule_dialogresource_head));

    if (!SWELL_curmodule_dialogresource_head)
    {
        DBG("JsfxEditorWindow::open() - Resource head is NULL!");
        return;
    }

    // Debug: List all available dialog resources
    DBG("JsfxEditorWindow::open() - Listing available dialog resources:");
    auto* p = SWELL_curmodule_dialogresource_head;
    int count = 0;
    while (p && count < 20) // Limit to prevent infinite loop
    {
        DBG("  Resource #"
            + juce::String(count)
            + ": resid="
            + juce::String::toHexString((juce::pointer_sized_int)p->resid)
            + " title="
            + juce::String(p->title ? p->title : "(null)"));
        p = p->_next;
        count++;
    }

    const char* targetResId = MAKEINTRESOURCE(IDD_JSDEBUG);
    DBG("JsfxEditorWindow::open() - Looking for resource ID: "
        + juce::String::toHexString((juce::pointer_sized_int)targetResId)
        + " (IDD_JSDEBUG="
        + juce::String(IDD_JSDEBUG)
        + ")");

    // Cast the mangled _watchDlgProc function to DLGPROC
    auto dlgproc = (DLGPROC)_ZN11SX_Instance13_watchDlgProcEP6HWND__jml;

    DBG("JsfxEditorWindow::open() - Calling SWELL_CreateDialog...");
    editorWindowHandle =
        SWELL_CreateDialog(SWELL_curmodule_dialogresource_head, targetResId, parentHwnd, dlgproc, (LPARAM)instance);

    if (editorWindowHandle)
    {
        DBG("JsfxEditorWindow::open() - Editor dialog created successfully!");
        // Store reference in JSFX instance
        instance->m_hwndwatch = (HWND)editorWindowHandle;

        // Show the editor window
        ShowWindow((HWND)editorWindowHandle, SW_SHOW);
        DBG("JsfxEditorWindow::open() - Editor window shown");
    }
    else
    {
        DBG("JsfxEditorWindow::open() - FAILED to create editor dialog!");
        currentInstance = nullptr;
    }
#else
    // Windows: JSFX editor is not yet supported on Windows
    DBG("JsfxEditorWindow::open() - Editor window not supported on Windows yet");

    // Show a message to the user
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        "Editor Not Available",
        "The JSFX code editor is currently only available on Linux and macOS.\n\n"
        "You can edit the JSFX file externally with your preferred text editor.",
        "OK"
    );
#endif
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
    return editorWindowHandle != nullptr && IsWindow((HWND)editorWindowHandle);
}

void JsfxEditorWindow::bringToFront()
{
    if (isOpen())
        SetForegroundWindow((HWND)editorWindowHandle);
}
