#include "JsfxLiceComponent.h"

#include "../jsfx/include/jsfx.h"
#include "PluginProcessor.h"

// Use the same defines as sfxui.cpp to safely include eel_lice.h
#define EEL_LICE_STANDALONE_NOINITQUIT
#define EEL_LICE_WANT_STANDALONE
#define EEL_LICE_API_ONLY
#include "WDL/eel2/eel_lice.h"

JsfxLiceComponent::JsfxLiceComponent()
    : processor(nullptr)
    , lastFramebufferWidth(0)
    , lastFramebufferHeight(0)
{
    // Enable mouse event tracking
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    setInterceptsMouseClicks(true, false); // This component intercepts mouse clicks

    startTimer(33); // 30fps polling
}

JsfxLiceComponent::~JsfxLiceComponent()
{
    stopTimer();
}

AudioPluginAudioProcessor* JsfxLiceComponent::getProcessor()
{
    if (processor)
        return processor;

    // Lazy init: find AudioProcessorEditor parent and get processor
    auto* parent = getParentComponent();
    while (parent != nullptr)
    {
        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(parent))
        {
            processor = dynamic_cast<AudioPluginAudioProcessor*>(editor->getAudioProcessor());
            return processor;
        }
        parent = parent->getParentComponent();
    }

    return nullptr;
}

SX_Instance* JsfxLiceComponent::getSXInstancePtr()
{
    auto* proc = getProcessor();
    if (proc)
        return proc->getSXInstancePtr();
    return nullptr;
}

void JsfxLiceComponent::paint(juce::Graphics& g)
{
    auto* instance = getSXInstancePtr();
    if (!instance)
    {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        g.drawText("No processor", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Access the LICE state directly from the JSFX instance
    auto* liceState = instance->m_lice_state;
    if (!liceState)
    {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        g.drawText("No LICE state - JSFX may not have @gfx section", getLocalBounds(), juce::Justification::centred);
        return;
    }

    if (!liceState->m_framebuffer)
    {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        g.drawText("No LICE framebuffer - graphics not initialized", getLocalBounds(), juce::Justification::centred);
        return;
    }

    LICE_IBitmap* fb = liceState->m_framebuffer;

    int width = fb->getWidth();
    int height = fb->getHeight();
    LICE_pixel* bits = fb->getBits();
    int rowSpan = fb->getRowSpan();
    bool isFlipped = fb->isFlipped();

    if (!bits || width <= 0 || height <= 0)
    {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        g.drawText("Empty JSFX framebuffer", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Fill background with black
    g.fillAll(juce::Colours::black);

    // Resize cached image if dimensions changed
    if (!cachedLiceImage.isValid() || cachedLiceImage.getWidth() != width || cachedLiceImage.getHeight() != height)
        cachedLiceImage = juce::Image(juce::Image::ARGB, width, height, false);

    // Copy LICE bitmap data to JUCE image
    // Use explicit scope to ensure BitmapData is destroyed before we draw the image
    {
        juce::Image::BitmapData destData(cachedLiceImage, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < height; ++y)
        {
            int srcY = isFlipped ? (height - 1 - y) : y;
            const LICE_pixel* srcRow = bits + (srcY * rowSpan);
            uint8_t* destRow = destData.getLinePointer(y);

            for (int x = 0; x < width; ++x)
            {
                // LICE pixel format: 0xAARRGGBB
                LICE_pixel srcPixel = srcRow[x];

                // Extract ARGB components
                uint8_t alpha = (srcPixel >> 24) & 0xFF;
                uint8_t red = (srcPixel >> 16) & 0xFF;
                uint8_t green = (srcPixel >> 8) & 0xFF;
                uint8_t blue = srcPixel & 0xFF;

                // JUCE ARGB format (little-endian: BGRA)
                destRow[x * 4 + 0] = blue;
                destRow[x * 4 + 1] = green;
                destRow[x * 4 + 2] = red;
                destRow[x * 4 + 3] = alpha;
            }
        }
    } // BitmapData is destroyed here

    // Draw the LICE framebuffer
    g.drawImageAt(cachedLiceImage, 0, 0);

    // Update cached dimensions
    lastFramebufferWidth = width;
    lastFramebufferHeight = height;
}

void JsfxLiceComponent::resized()
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return;

    auto* liceState = instance->m_lice_state;
    if (!liceState || !liceState->m_framebuffer)
        return;

    // When component is resized, update JSFX dimensions and reinitialize
    int newWidth = getWidth();
    int newHeight = getHeight();

    if (newWidth > 0 && newHeight > 0 && (newWidth != lastFramebufferWidth || newHeight != lastFramebufferHeight))
    {
        // Recreate framebuffer with new dimensions
        if (!instance->m_in_gfx)
        {
            instance->m_in_gfx++;
            instance->m_init_mutex.Enter();

            RECT r;
            r.left = 0;
            r.top = 0;
            r.right = newWidth;
            r.bottom = newHeight;

            // This will resize the framebuffer
            liceState->setup_frame(nullptr, r);

            // Update our tracking variables
            lastFramebufferWidth = newWidth;
            lastFramebufferHeight = newHeight;

            instance->m_init_mutex.Leave();
            instance->m_in_gfx--;

            repaint();
        }
    }
}

void JsfxLiceComponent::triggerGfxExecution()
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return;

    auto* liceState = instance->m_lice_state;
    if (!liceState || !liceState->m_framebuffer)
        return;

    // Run @gfx immediately to respond to user input
    if (!instance->m_in_gfx)
    {
        instance->m_in_gfx++;
        instance->m_init_mutex.Enter();

        // Check if we need to call on_slider_change (parameters changed)
        if (instance->m_slider_anychanged)
        {
            instance->m_mutex.Enter();
            instance->on_slider_change();
            instance->m_mutex.Leave();
        }

        // Run the @gfx section
        instance->gfx_runCode(0);

        instance->m_init_mutex.Leave();
        instance->m_in_gfx--;

        // Repaint to show the changes
        repaint();
    }
}

void JsfxLiceComponent::timerCallback()
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return;

    auto* liceState = instance->m_lice_state;
    if (!liceState || !liceState->m_framebuffer)
        return;

    // Continuously run the @gfx section (like sfxui.cpp does in its timer)
    // This keeps the graphics updating in real-time
    if (!instance->m_in_gfx)
    {
        instance->m_in_gfx++;

        // Always acquire mutex for safety
        instance->m_init_mutex.Enter();

        // Check if we need to call on_slider_change (parameters changed)
        // This ensures @slider section runs before @gfx
        if (instance->m_slider_anychanged)
        {
            instance->m_mutex.Enter();
            instance->on_slider_change();
            instance->m_mutex.Leave();
        }

        // Run the @gfx section to update graphics
        instance->gfx_runCode(0);

        instance->m_init_mutex.Leave();
        instance->m_in_gfx--;

        // Always repaint after running @gfx, don't rely on dirty flag
        // The dirty flag might not be set consistently
        repaint();
    }
}

void JsfxLiceComponent::mouseDown(const juce::MouseEvent& event)
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return;

    // Update JSFX mouse variables
    updateMousePosition(event);
    updateMouseButtons(event);
}

void JsfxLiceComponent::mouseUp(const juce::MouseEvent& event)
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return;

    auto* liceState = instance->m_lice_state;
    if (!liceState)
        return;

    // Update position first
    updateMousePosition(event);

    // For mouseUp, we need to explicitly clear button flags
    // because JUCE might still report the button as down in the event
    int mouseCap = 0;

    // Only keep modifier keys, clear all mouse buttons
    if (event.mods.isCtrlDown())
        mouseCap |= 4;
    if (event.mods.isShiftDown())
        mouseCap |= 8;
    if (event.mods.isAltDown())
        mouseCap |= 16;

    // Set the cleared button state
    if (liceState->m_mouse_cap)
        *liceState->m_mouse_cap = static_cast<EEL_F>(mouseCap);
}

void JsfxLiceComponent::mouseDrag(const juce::MouseEvent& event)
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return;

    // Update JSFX mouse variables
    updateMousePosition(event);
    updateMouseButtons(event);
}

void JsfxLiceComponent::mouseMove(const juce::MouseEvent& event)
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return;

    // Update JSFX mouse variables (position only, no buttons for move)
    updateMousePosition(event);

    // Update modifiers even on mouse move (for hover effects with modifiers)
    auto* liceState = instance->m_lice_state;
    if (liceState && liceState->m_mouse_cap)
    {
        int mouseCap = 0;
        if (event.mods.isCtrlDown())
            mouseCap |= 4;
        if (event.mods.isShiftDown())
            mouseCap |= 8;
        if (event.mods.isAltDown())
            mouseCap |= 16;
        *liceState->m_mouse_cap = static_cast<EEL_F>(mouseCap);
    }
}

void JsfxLiceComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return;

    auto* liceState = instance->m_lice_state;
    if (!liceState)
        return;

    // Update mouse position first
    updateMousePosition(event);

    // Update gfx_mouse_wheel variable
    // JSFX expects: positive = scroll up, negative = scroll down
    // JUCE wheel.deltaY: positive = scroll up, negative = scroll down (same convention)
    float wheelDelta = wheel.deltaY * 120.0f; // Scale to match typical mouse wheel units

    if (liceState->m_mouse_wheel)
        *liceState->m_mouse_wheel = static_cast<EEL_F>(wheelDelta);

    // Update button state
    updateMouseButtons(event);
}

// Helper methods to update JSFX mouse variables
void JsfxLiceComponent::updateMousePosition(const juce::MouseEvent& event)
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return;

    auto* liceState = instance->m_lice_state;
    if (!liceState || !liceState->m_framebuffer)
        return;

    // Update LICE state's mouse position variables directly
    if (liceState->m_mouse_x && liceState->m_mouse_y)
    {
        *liceState->m_mouse_x = static_cast<EEL_F>(event.x);
        *liceState->m_mouse_y = static_cast<EEL_F>(event.y);
    }
}

void JsfxLiceComponent::updateMouseButtons(const juce::MouseEvent& event)
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return;

    auto* liceState = instance->m_lice_state;
    if (!liceState)
        return;

    // Update gfx_mouse_cap (mouse button state)
    int mouseCap = 0;
    if (event.mods.isLeftButtonDown())
        mouseCap |= 1;
    if (event.mods.isRightButtonDown())
        mouseCap |= 2;
    if (event.mods.isMiddleButtonDown())
        mouseCap |= 64;
    if (event.mods.isCtrlDown())
        mouseCap |= 4;
    if (event.mods.isShiftDown())
        mouseCap |= 8;
    if (event.mods.isAltDown())
        mouseCap |= 16;

    // Update LICE state's mouse_cap variable directly
    if (liceState->m_mouse_cap)
        *liceState->m_mouse_cap = static_cast<EEL_F>(mouseCap);
}

bool JsfxLiceComponent::keyPressed(const juce::KeyPress& key)
{
    // TODO: Forward keyboard events to JSFX
    juce::ignoreUnused(key);
    return false;
}

juce::Rectangle<int> JsfxLiceComponent::getRecommendedBounds()
{
    auto* instance = getSXInstancePtr();
    if (!instance)
        return juce::Rectangle<int>(0, 0, 400, 300);

    // Check if JSFX has requested specific dimensions via gfx(width, height) call
    // These are set during @init section execution
    if (instance->m_gfx_reqw > 0 && instance->m_gfx_reqh > 0)
        return juce::Rectangle<int>(0, 0, instance->m_gfx_reqw, instance->m_gfx_reqh);

    auto* liceState = instance->m_lice_state;
    if (liceState)
    {
        // Fall back to gfx_w/gfx_h variables if available
        if (liceState->m_gfx_w && liceState->m_gfx_h)
        {
            int width = static_cast<int>(*liceState->m_gfx_w);
            int height = static_cast<int>(*liceState->m_gfx_h);

            if (width > 0 && height > 0)
                return juce::Rectangle<int>(0, 0, width, height);
        }

        // Fall back to framebuffer dimensions
        if (liceState->m_framebuffer)
        {
            LICE_IBitmap* fb = liceState->m_framebuffer;
            int width = fb->getWidth();
            int height = fb->getHeight();

            if (width > 0 && height > 0)
                return juce::Rectangle<int>(0, 0, width, height);
        }
    }

    // Final fallback
    return juce::Rectangle<int>(0, 0, 400, 300);
}
