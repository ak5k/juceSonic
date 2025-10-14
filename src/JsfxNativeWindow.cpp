#include "JsfxNativeWindow.h"

#include "JsfxHelper.h"

#include <jsfx/sfxui.h>
#include <juce_gui_basics/juce_gui_basics.h>

class JsfxNativeWindow::HostComponent : public juce::Component
{
public:
    HostComponent(SX_Instance* inst)
        : sxInstance(inst)
    {
        setOpaque(true);
    }

    ~HostComponent() override
    {
        destroyNative();
    }

    void createNative()
    {
        if (nativeUIHandle || !sxInstance)
            return;

        void* parent = getWindowHandle();
        if (!parent)
            return;

        // Create JSFX UI using helper (isolates Win32/SWELL from JUCE)
        nativeUIHandle = JsfxHelper::createJsfxUI(sxInstance, parent);

        if (nativeUIHandle)
        {
            // Get the actual size that the JSFX UI dialog wants to be
            auto uiSize = JsfxHelper::getJsfxUISize(nativeUIHandle);

            // Notify parent that we want to resize to accommodate the dialog
            if (auto* parentWindow = findParentComponentOfClass<JsfxNativeWindow>())
                parentWindow->resizeForDialog(uiSize.width, uiSize.height);

            // Position and show the dialog
            JsfxHelper::positionJsfxUI(nativeUIHandle, 0, 0, uiSize.width, uiSize.height);
            JsfxHelper::showJsfxUI(nativeUIHandle, true);
        }
    }

    void destroyNative()
    {
        if (nativeUIHandle)
        {
            JsfxHelper::destroyJsfxUI(sxInstance, nativeUIHandle);
            nativeUIHandle = nullptr;
        }
    }

    void resized() override
    {
        if (nativeUIHandle)
        {
            auto r = getLocalBounds();
            JsfxHelper::positionJsfxUI(nativeUIHandle, 0, 0, r.getWidth(), r.getHeight());
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);
    }

private:
    SX_Instance* sxInstance = nullptr;
    void* nativeUIHandle = nullptr;
};

JsfxNativeWindow::JsfxNativeWindow(SX_Instance* instance, const juce::String& title)
    : DocumentWindow(title, juce::Colours::darkgrey, allButtons)
    , sxInstance(instance)
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    host = std::make_unique<HostComponent>(sxInstance);
    setContentNonOwned(host.get(), true);

    // Start with a small default size - will be resized based on actual dialog size
    centreWithSize(100, 100);
    setVisible(true);

    // Create the native UI - this will trigger resizeForDialog callback
    host->createNative();
}

JsfxNativeWindow::~JsfxNativeWindow()
{
    if (host)
        host->destroyNative();
}

void JsfxNativeWindow::closeButtonPressed()
{
    if (host)
        host->destroyNative();
    setVisible(false);
}

void JsfxNativeWindow::resizeForDialog(int width, int height)
{
    // Resize the content component to match the dialog size
    if (host)
        host->setSize(width, height);

    // Resize the window to accommodate the content plus title bar
    // The DocumentWindow will automatically add space for the title bar
    setSize(width, height + getTitleBarHeight());

    // Re-center the window with the new size
    centreWithSize(width, height + getTitleBarHeight());
}
