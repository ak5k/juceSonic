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
    if (key == juce::KeyPress::F11Key)
    {
        if (onWindowClosed)
            juce::MessageManager::callAsync([callback = onWindowClosed]() { callback(); });
        return true;
    }

    if (key == juce::KeyPress::escapeKey && isFullScreen())
    {
        setFullScreen(false);
        return true;
    }

    return ResizableWindow::keyPressed(key);
}

void JsfxLiceFullscreenWindow::showWithComponent(JsfxLiceComponent* component)
{
    if (!component)
        return;

    setContentNonOwned(component, false);

    auto recommendedBounds = component->getRecommendedBounds();
    if (recommendedBounds.getWidth() > 0 && recommendedBounds.getHeight() > 0)
        centreWithSize(recommendedBounds.getWidth(), recommendedBounds.getHeight());
    else
        centreWithSize(800, 600);

    setVisible(true);
    toFront(true);
}
