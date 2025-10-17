#include "IOMatrixComponent.h"

//==============================================================================
// IOMatrixContent implementation
IOMatrixContent::IOMatrixContent(
    int numJuceInputs,
    int numJuceSidechains,
    int numJuceOutputs,
    int numJsfxInputs,
    int numJsfxSidechains,
    int numJsfxOutputs
)
    : numJuceIns(numJuceInputs)
    , numJuceScs(numJuceSidechains)
    , numJuceOuts(numJuceOutputs)
    , numJsfxIns(numJsfxInputs)
    , numJsfxScs(numJsfxSidechains)
    , numJsfxOuts(numJsfxOutputs)
{
    createCells();
    resetToDefaults();
    setSize(getIdealWidth(), getIdealHeight());
}

void IOMatrixContent::createCells()
{
    // Create input cells (JUCE inputs -> JSFX inputs)
    if (numJuceIns > 0 && numJsfxIns > 0)
    {
        for (int r = 0; r < numJuceIns; ++r)
        {
            for (int c = 0; c < numJsfxIns; ++c)
            {
                auto cell = std::make_unique<RoutingCell>(r, c, [this](int, int) { handleCellChange(); });
                addAndMakeVisible(cell.get());
                inputCells.push_back(std::move(cell));
            }
        }
    }

    // Create sidechain cells (JUCE sidechain -> JSFX channels)
    if (numJuceScs > 0 && numJsfxScs > 0)
    {
        for (int r = 0; r < numJuceScs; ++r)
        {
            for (int c = 0; c < numJsfxScs; ++c)
            {
                auto cell = std::make_unique<RoutingCell>(r, c, [this](int, int) { handleCellChange(); });
                addAndMakeVisible(cell.get());
                sidechainCells.push_back(std::move(cell));
            }
        }
    }

    // Create output cells (JSFX outputs -> JUCE outputs)
    if (numJsfxOuts > 0 && numJuceOuts > 0)
    {
        for (int r = 0; r < numJsfxOuts; ++r)
        {
            for (int c = 0; c < numJuceOuts; ++c)
            {
                auto cell = std::make_unique<RoutingCell>(r, c, [this](int, int) { handleCellChange(); });
                addAndMakeVisible(cell.get());
                outputCells.push_back(std::move(cell));
            }
        }
    }
}

void IOMatrixContent::paint(juce::Graphics& g)
{
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(findColour(juce::Label::textColourId));
    g.setFont(11.0f);

    int xPos = labelWidth;
    int yPos = topLabelHeight;

    // Draw INPUT section
    if (numJuceIns > 0 && numJsfxIns > 0)
    {
        // Section title
        g.setFont(13.0f);
        g.drawText(
            "INPUT",
            xPos,
            0,
            numJsfxIns * (cellSize + spacing),
            topLabelHeight / 2,
            juce::Justification::centred
        );

        // JSFX channel numbers (horizontal, top)
        g.setFont(10.0f);
        for (int c = 0; c < numJsfxIns; ++c)
        {
            int x = xPos + c * (cellSize + spacing);
            g.drawText(
                juce::String(c + 1),
                x,
                topLabelHeight / 2,
                cellSize,
                topLabelHeight / 2,
                juce::Justification::centred
            );
        }

        // JUCE channel numbers (vertical, left)
        for (int r = 0; r < numJuceIns; ++r)
        {
            int y = yPos + r * (cellSize + spacing);
            g.drawText(juce::String(r + 1), 0, y, labelWidth - 5, cellSize, juce::Justification::centredRight);
        }

        xPos += numJsfxIns * (cellSize + spacing) + sectionGap;
    }

    // Draw SIDECHAIN section
    if (numJuceScs > 0 && numJsfxScs > 0)
    {
        // Section title
        g.setFont(13.0f);
        g.drawText(
            "SIDECHAIN",
            xPos,
            0,
            numJsfxScs * (cellSize + spacing),
            topLabelHeight / 2,
            juce::Justification::centred
        );

        // JSFX channel numbers (horizontal, top)
        g.setFont(10.0f);
        for (int c = 0; c < numJsfxScs; ++c)
        {
            int x = xPos + c * (cellSize + spacing);
            g.drawText(
                juce::String(c + 1),
                x,
                topLabelHeight / 2,
                cellSize,
                topLabelHeight / 2,
                juce::Justification::centred
            );
        }

        // JUCE channel numbers (vertical, left)
        for (int r = 0; r < numJuceScs; ++r)
        {
            int y = yPos + r * (cellSize + spacing);
            g.drawText(juce::String(r + 1), 0, y, labelWidth - 5, cellSize, juce::Justification::centredRight);
        }

        xPos += numJsfxScs * (cellSize + spacing) + sectionGap;
    }

    // Draw OUTPUT section
    if (numJsfxOuts > 0 && numJuceOuts > 0)
    {
        // Section title
        g.setFont(13.0f);
        g.drawText(
            "OUTPUT",
            xPos,
            0,
            numJsfxOuts * (cellSize + spacing),
            topLabelHeight / 2,
            juce::Justification::centred
        );

        // JSFX channel numbers (horizontal, top)
        g.setFont(10.0f);
        for (int c = 0; c < numJsfxOuts; ++c)
        {
            int x = xPos + c * (cellSize + spacing);
            g.drawText(
                juce::String(c + 1),
                x,
                topLabelHeight / 2,
                cellSize,
                topLabelHeight / 2,
                juce::Justification::centred
            );
        }

        // JUCE channel numbers (vertical, RIGHT side for output)
        int rightLabelX = xPos + numJsfxOuts * (cellSize + spacing) + 5;
        for (int r = 0; r < numJuceOuts; ++r)
        {
            int y = yPos + r * (cellSize + spacing);
            g.drawText(juce::String(r + 1), rightLabelX, y, labelWidth - 5, cellSize, juce::Justification::centredLeft);
        }
    }
}

void IOMatrixContent::resized()
{
    int xPos = labelWidth;
    int yPos = topLabelHeight;

    // Position INPUT cells
    if (numJuceIns > 0 && numJsfxIns > 0)
    {
        for (int r = 0; r < numJuceIns; ++r)
        {
            for (int c = 0; c < numJsfxIns; ++c)
            {
                int x = xPos + c * (cellSize + spacing);
                int y = yPos + r * (cellSize + spacing);
                inputCells[r * numJsfxIns + c]->setBounds(x, y, cellSize, cellSize);
            }
        }
        xPos += numJsfxIns * (cellSize + spacing) + sectionGap;
    }

    // Position SIDECHAIN cells
    if (numJuceScs > 0 && numJsfxScs > 0)
    {
        for (int r = 0; r < numJuceScs; ++r)
        {
            for (int c = 0; c < numJsfxScs; ++c)
            {
                int x = xPos + c * (cellSize + spacing);
                int y = yPos + r * (cellSize + spacing);
                sidechainCells[r * numJsfxScs + c]->setBounds(x, y, cellSize, cellSize);
            }
        }
        xPos += numJsfxScs * (cellSize + spacing) + sectionGap;
    }

    // Position OUTPUT cells
    if (numJsfxOuts > 0 && numJuceOuts > 0)
    {
        for (int r = 0; r < numJsfxOuts; ++r)
        {
            for (int c = 0; c < numJuceOuts; ++c)
            {
                int x = xPos + c * (cellSize + spacing);
                int y = yPos + r * (cellSize + spacing);
                outputCells[r * numJuceOuts + c]->setBounds(x, y, cellSize, cellSize);
            }
        }
    }
}

int IOMatrixContent::getIdealWidth() const
{
    int width = labelWidth;

    if (numJuceIns > 0 && numJsfxIns > 0)
        width += numJsfxIns * (cellSize + spacing) + sectionGap;
    if (numJuceScs > 0 && numJsfxScs > 0)
        width += numJsfxScs * (cellSize + spacing) + sectionGap;
    if (numJsfxOuts > 0 && numJuceOuts > 0)
        width += numJsfxOuts * (cellSize + spacing) + labelWidth; // Extra label space on right for output

    return width + 20; // Extra padding
}

int IOMatrixContent::getIdealHeight() const
{
    int maxRows = juce::jmax(numJuceIns, numJuceScs, numJsfxOuts);
    return topLabelHeight + maxRows * (cellSize + spacing) + 20; // Extra padding
}

juce::String IOMatrixContent::getRoutingState() const
{
    juce::String state;

    // Encode input cells
    if (numJuceIns > 0 && numJsfxIns > 0)
    {
        juce::String inputStr;
        for (const auto& cell : inputCells)
            inputStr += cell->getActive() ? "1" : "0";
        state += inputStr;
    }
    state += ",";

    // Encode sidechain cells
    if (numJuceScs > 0 && numJsfxScs > 0)
    {
        juce::String scStr;
        for (const auto& cell : sidechainCells)
            scStr += cell->getActive() ? "1" : "0";
        state += scStr;
    }
    state += ",";

    // Encode output cells
    if (numJsfxOuts > 0 && numJuceOuts > 0)
    {
        juce::String outStr;
        for (const auto& cell : outputCells)
            outStr += cell->getActive() ? "1" : "0";
        state += outStr;
    }

    return state;
}

void IOMatrixContent::setRoutingState(const juce::String& state)
{
    if (state.isEmpty())
    {
        resetToDefaults();
        return;
    }

    auto parts = juce::StringArray::fromTokens(state, ",", "");
    if (parts.size() != 3)
        return;

    // Decode input cells
    if (!inputCells.empty() && parts[0].isNotEmpty())
    {
        int idx = 0;
        for (auto& cell : inputCells)
        {
            if (idx < parts[0].length())
                cell->setActive(parts[0][idx] == '1');
            idx++;
        }
    }

    // Decode sidechain cells
    if (!sidechainCells.empty() && parts[1].isNotEmpty())
    {
        int idx = 0;
        for (auto& cell : sidechainCells)
        {
            if (idx < parts[1].length())
                cell->setActive(parts[1][idx] == '1');
            idx++;
        }
    }

    // Decode output cells
    if (!outputCells.empty() && parts[2].isNotEmpty())
    {
        int idx = 0;
        for (auto& cell : outputCells)
        {
            if (idx < parts[2].length())
                cell->setActive(parts[2][idx] == '1');
            idx++;
        }
    }
}

void IOMatrixContent::resetToDefaults()
{
    // Clear all input cells, then set diagonal
    for (auto& cell : inputCells)
        cell->setActive(false);
    int maxDiag = juce::jmin(numJuceIns, numJsfxIns);
    for (int i = 0; i < maxDiag; ++i)
        inputCells[i * numJsfxIns + i]->setActive(true);

    // Clear all sidechain cells, then set diagonal
    for (auto& cell : sidechainCells)
        cell->setActive(false);
    maxDiag = juce::jmin(numJuceScs, numJsfxScs);
    for (int i = 0; i < maxDiag; ++i)
        sidechainCells[i * numJsfxScs + i]->setActive(true);

    // Clear all output cells, then set diagonal
    for (auto& cell : outputCells)
        cell->setActive(false);
    maxDiag = juce::jmin(numJsfxOuts, numJuceOuts);
    for (int i = 0; i < maxDiag; ++i)
        outputCells[i * numJuceOuts + i]->setActive(true);

    handleCellChange();
}

bool IOMatrixContent::getInputRouting(int juceChannel, int jsfxChannel) const
{
    if (juceChannel < 0 || juceChannel >= numJuceIns || jsfxChannel < 0 || jsfxChannel >= numJsfxIns)
        return false;
    return inputCells[juceChannel * numJsfxIns + jsfxChannel]->getActive();
}

bool IOMatrixContent::getSidechainRouting(int juceChannel, int jsfxChannel) const
{
    if (juceChannel < 0 || juceChannel >= numJuceScs || jsfxChannel < 0 || jsfxChannel >= numJsfxScs)
        return false;
    return sidechainCells[juceChannel * numJsfxScs + jsfxChannel]->getActive();
}

bool IOMatrixContent::getOutputRouting(int jsfxChannel, int juceChannel) const
{
    if (jsfxChannel < 0 || jsfxChannel >= numJsfxOuts || juceChannel < 0 || juceChannel >= numJuceOuts)
        return false;
    return outputCells[jsfxChannel * numJuceOuts + juceChannel]->getActive();
}

void IOMatrixContent::handleCellChange()
{
    if (onRoutingChanged)
        onRoutingChanged();
}

//==============================================================================
// IOMatrixComponent implementation (wrapper with viewport)
IOMatrixComponent::IOMatrixComponent(
    int numJuceInputs,
    int numJuceSidechains,
    int numJuceOutputs,
    int numJsfxInputs,
    int numJsfxSidechains,
    int numJsfxOutputs
)
{
    // Create reset button
    resetButton = std::make_unique<juce::TextButton>("Reset");
    addAndMakeVisible(resetButton.get());

    // Create content
    content = std::make_unique<IOMatrixContent>(
        numJuceInputs,
        numJuceSidechains,
        numJuceOutputs,
        numJsfxInputs,
        numJsfxSidechains,
        numJsfxOutputs
    );

    // Setup reset button callback
    resetButton->onClick = [this]() { content->resetToDefaults(); };

    // Forward onRoutingChanged from content
    content->onRoutingChanged = [this]()
    {
        if (onRoutingChanged)
            onRoutingChanged();
    };

    // Create viewport
    viewport = std::make_unique<juce::Viewport>();
    viewport->setViewedComponent(content.get(), false);
    addAndMakeVisible(viewport.get());

    // Calculate ideal size
    auto idealBounds = getIdealBounds();
    setSize(idealBounds.getWidth(), idealBounds.getHeight());
}

void IOMatrixComponent::paint(juce::Graphics& g)
{
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void IOMatrixComponent::resized()
{
    auto bounds = getLocalBounds();

    // Reset button at top right corner
    auto topBar = bounds.removeFromTop(35);
    resetButton->setBounds(topBar.removeFromRight(80).reduced(5));

    // Viewport takes remaining space
    viewport->setBounds(bounds);
}

juce::Rectangle<int> IOMatrixComponent::getIdealBounds() const
{
    if (!content)
        return {400, 300};

    // Get screen dimensions
    auto displays = juce::Desktop::getInstance().getDisplays();
    auto* display = displays.getPrimaryDisplay();
    if (!display)
        return {400, 300};

    auto screenArea = display->userArea;
    int maxWidth = static_cast<int>(screenArea.getWidth() * 2.0 / 3.0);
    int maxHeight = static_cast<int>(screenArea.getHeight() * 2.0 / 3.0);

    // Get content's ideal size
    int contentWidth = content->getIdealWidth();
    int contentHeight = content->getIdealHeight();

    // Add space for reset button and padding
    int totalWidth = juce::jmin(contentWidth + 20, maxWidth);
    int totalHeight = juce::jmin(contentHeight + 50, maxHeight); // +35 for button, +15 padding

    return {totalWidth, totalHeight};
}

juce::String IOMatrixComponent::getRoutingState() const
{
    return content ? content->getRoutingState() : juce::String();
}

void IOMatrixComponent::setRoutingState(const juce::String& state)
{
    if (content)
        content->setRoutingState(state);
}

void IOMatrixComponent::resetToDefaults()
{
    if (content)
        content->resetToDefaults();
}

bool IOMatrixComponent::getInputRouting(int juceChannel, int jsfxChannel) const
{
    return content ? content->getInputRouting(juceChannel, jsfxChannel) : false;
}

bool IOMatrixComponent::getSidechainRouting(int juceChannel, int jsfxChannel) const
{
    return content ? content->getSidechainRouting(juceChannel, jsfxChannel) : false;
}

bool IOMatrixComponent::getOutputRouting(int jsfxChannel, int juceChannel) const
{
    return content ? content->getOutputRouting(jsfxChannel, juceChannel) : false;
}
