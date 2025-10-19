#include "JsfxLiceFullscreenWindow.h"
#include "JsfxLiceComponent.h"

JsfxLiceFullscreenWindow::JsfxLiceFullscreenWindow()
    : ResizableWindow("JSFX", juce::Colours::black, true)
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
}

void JsfxLiceFullscreenWindow::closeButtonPressed()
{
    if (onWindowClosed)
        juce::MessageManager::callAsync([callback = onWindowClosed]() { callback(); });
}

bool JsfxLiceFullscreenWindow::keyPressed(const juce::KeyPress& key)
{
    // F11 or ESC - Exit fullscreen
    if (key == juce::KeyPress::F11Key || key == juce::KeyPress::escapeKey)
    {
        if (onWindowClosed)
            juce::MessageManager::callAsync([callback = onWindowClosed]() { callback(); });
        return true;
    }

    return ResizableWindow::keyPressed(key);
}

void JsfxLiceFullscreenWindow::showWithComponent(JsfxLiceComponent* liceComponent)
{
    if (!liceComponent)
        return;

    setContentNonOwned(liceComponent, false);

    // Size the window to match the LICE component size
    centreWithSize(liceComponent->getWidth(), liceComponent->getHeight());

    setVisible(true);
    toFront(true);
}
