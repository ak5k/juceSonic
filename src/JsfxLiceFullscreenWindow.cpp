#include "JsfxLiceFullscreenWindow.h"
#include "JsfxLiceComponent.h"

JsfxLiceFullscreenWindow::JsfxLiceFullscreenWindow()
    : ResizableWindow("JSFX", juce::Colours::black, true) // With border initially
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setVisible(false);
}

JsfxLiceFullscreenWindow::~JsfxLiceFullscreenWindow()
{
    // We only borrow the component, don't clear or delete it
    // The PluginEditor owns it and will manage its lifecycle
}

void JsfxLiceFullscreenWindow::closeButtonPressed()
{
    // Notify parent to handle the close
    if (onWindowClosed)
    {
        // Defer the callback to avoid modifying component hierarchy during event processing
        juce::MessageManager::callAsync([callback = onWindowClosed]() { callback(); });
    }
}

bool JsfxLiceFullscreenWindow::keyPressed(const juce::KeyPress& key)
{
    // F11 to close window and return to embedded view
    if (key == juce::KeyPress::F11Key)
    {
        if (onWindowClosed)
        {
            // Defer the callback to avoid modifying component hierarchy during event processing
            juce::MessageManager::callAsync([callback = onWindowClosed]() { callback(); });
        }
        return true;
    }

    // Escape to exit fullscreen (but don't close window)
    if (key == juce::KeyPress::escapeKey && isFullScreen())
    {
        setFullScreen(false);
        return true;
    }

    return ResizableWindow::keyPressed(key);
}

void JsfxLiceFullscreenWindow::resized()
{
    // ResizableWindow handles content component sizing automatically
    ResizableWindow::resized();
}

void JsfxLiceFullscreenWindow::showWithComponent(JsfxLiceComponent* component)
{
    if (!component)
        return;

    liceComponent = component;

    // Use setContentNonOwned - we don't own the component, just borrow it
    setContentNonOwned(liceComponent, false);

    // Get recommended size from JSFX
    auto recommendedBounds = liceComponent->getRecommendedBounds();
    if (recommendedBounds.getWidth() > 0 && recommendedBounds.getHeight() > 0)
        centreWithSize(recommendedBounds.getWidth(), recommendedBounds.getHeight());
    else
        centreWithSize(800, 600);

    // Show the window
    setVisible(true);
    toFront(true);
}
