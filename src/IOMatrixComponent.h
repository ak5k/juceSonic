#pragma once

#include <Config.h>

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Single clickable cell in the routing matrix
class RoutingCell : public juce::Component
{
public:
    RoutingCell(int row, int col, std::function<void(int, int)> onClick)
        : rowIndex(row)
        , colIndex(col)
        , clickCallback(onClick)
    {
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);

        // Draw cell background
        if (isActive)
            g.setColour(juce::Colours::green.withAlpha(0.8f));
        else if (isMouseOver())
            g.setColour(juce::Colours::grey.withAlpha(0.5f));
        else
            g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));

        g.fillRect(bounds);

        // Draw cell border
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawRect(bounds, 1.0f);

        // Draw connection indicator
        if (isActive)
        {
            g.setColour(juce::Colours::white);
            auto center = bounds.getCentre();
            g.fillEllipse(center.x - 3, center.y - 3, 6, 6);
        }
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        isActive = !isActive;
        if (clickCallback)
            clickCallback(rowIndex, colIndex);
        repaint();
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        repaint();
    }

    void setActive(bool active)
    {
        isActive = active;
        repaint();
    }

    bool getActive() const
    {
        return isActive;
    }

private:
    int rowIndex, colIndex;
    bool isActive = false;
    std::function<void(int, int)> clickCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingCell)
};

//==============================================================================
// Matrix grid for one routing section (input, sidechain, or output)
class RoutingMatrix : public juce::Component
{
public:
    RoutingMatrix(const juce::String& title, int numRows, int numCols)
        : titleText(title)
        , rows(numRows)
        , cols(numCols)
    {
        // Create cells
        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                auto cell = std::make_unique<RoutingCell>(r, c, [this](int row, int col) { onCellClicked(row, col); });
                addAndMakeVisible(cell.get());
                cells.push_back(std::move(cell));
            }
        }
    }

    void paint(juce::Graphics& g) override
    {
        const int cellSize = 20;
        const int spacing = 2;
        const int labelWidth = 60;
        const int topLabelHeight = 40;

        g.setColour(juce::Colours::white);
        g.setFont(12.0f);

        // Draw title at the very top
        g.setFont(14.0f);
        auto titleArea = getLocalBounds().removeFromTop(25);
        g.drawText(titleText, titleArea, juce::Justification::centred);

        auto bounds = getLocalBounds();
        bounds.removeFromTop(25); // Skip title area

        // Draw column labels (JSFX channel names) horizontally at top
        g.setFont(10.0f);
        auto colLabelArea = bounds.removeFromTop(topLabelHeight);
        colLabelArea.removeFromLeft(labelWidth); // Skip corner
        for (int c = 0; c < cols; ++c)
        {
            auto labelBounds = colLabelArea.removeFromLeft(cellSize + spacing);
            g.drawText(juce::String(c + 1), labelBounds, juce::Justification::centred);
        }

        // Draw row labels (JUCE channel names) vertically on the left
        for (int r = 0; r < rows; ++r)
        {
            int y = 25 + topLabelHeight + r * (cellSize + spacing);
            auto labelBounds = juce::Rectangle<int>(0, y, labelWidth - 5, cellSize);
            g.drawText(juce::String(r + 1), labelBounds, juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        const int cellSize = 20;
        const int spacing = 2;
        const int labelWidth = 60;
        const int topLabelHeight = 40;

        // Calculate where the grid should start
        int gridStartX = labelWidth;
        int gridStartY = 25 + topLabelHeight;

        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                int x = gridStartX + c * (cellSize + spacing);
                int y = gridStartY + r * (cellSize + spacing);
                cells[r * cols + c]->setBounds(x, y, cellSize, cellSize);
            }
        }
    }

    int getPreferredWidth() const
    {
        const int cellSize = 20;
        const int spacing = 2;
        const int labelWidth = 60;
        return labelWidth + cols * (cellSize + spacing);
    }

    int getPreferredHeight() const
    {
        const int cellSize = 20;
        const int spacing = 2;
        const int topLabelHeight = 40;
        return 25 + topLabelHeight + rows * (cellSize + spacing); // +25 for title, +40 for column labels
    }

    void setConnection(int row, int col, bool active)
    {
        if (row >= 0 && row < rows && col >= 0 && col < cols)
            cells[row * cols + col]->setActive(active);
    }

    bool getConnection(int row, int col) const
    {
        if (row >= 0 && row < rows && col >= 0 && col < cols)
            return cells[row * cols + col]->getActive();
        return false;
    }

    void resetToDiagonal()
    {
        // Clear all connections
        for (auto& cell : cells)
            cell->setActive(false);

        // Set diagonal (1:1 mapping)
        int maxDiag = juce::jmin(rows, cols);
        for (int i = 0; i < maxDiag; ++i)
            cells[i * cols + i]->setActive(true);
    }

    std::function<void(int, int)> onCellClicked;

private:
    juce::String titleText;
    int rows, cols;
    std::vector<std::unique_ptr<RoutingCell>> cells;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingMatrix)
};

//==============================================================================
// Content component with unified grid layout
class IOMatrixContent : public juce::Component
{
public:
    IOMatrixContent(
        int numJuceInputs,
        int numJuceSidechains,
        int numJuceOutputs,
        int numJsfxInputs,
        int numJsfxSidechains,
        int numJsfxOutputs
    );

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Get/Set routing state as encoded string for APVTS
    juce::String getRoutingState() const;
    void setRoutingState(const juce::String& state);

    // Reset all matrices to diagonal (1:1) mapping
    void resetToDefaults();

    // Get routing configuration
    bool getInputRouting(int juceChannel, int jsfxChannel) const;
    bool getSidechainRouting(int juceChannel, int jsfxChannel) const;
    bool getOutputRouting(int jsfxChannel, int juceChannel) const;

    // Calculate ideal size for content
    int getIdealWidth() const;
    int getIdealHeight() const;

    std::function<void()> onRoutingChanged;

private:
    // Cell storage for each section
    std::vector<std::unique_ptr<RoutingCell>> inputCells;
    std::vector<std::unique_ptr<RoutingCell>> sidechainCells;
    std::vector<std::unique_ptr<RoutingCell>> outputCells;

    int numJuceIns, numJuceScs, numJuceOuts;
    int numJsfxIns, numJsfxScs, numJsfxOuts;

    // Layout constants
    static constexpr int cellSize = 20;
    static constexpr int spacing = 2;
    static constexpr int labelWidth = 50;
    static constexpr int sectionGap = 30;
    static constexpr int topLabelHeight = 30;

    void handleCellChange();
    void createCells();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IOMatrixContent)
};

//==============================================================================
// Main I/O Matrix window with scrollable viewport
class IOMatrixComponent : public juce::Component
{
public:
    IOMatrixComponent(
        int numJuceInputs,
        int numJuceSidechains,
        int numJuceOutputs,
        int numJsfxInputs,
        int numJsfxSidechains,
        int numJsfxOutputs
    );

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Get/Set routing state as encoded string for APVTS
    juce::String getRoutingState() const;
    void setRoutingState(const juce::String& state);

    // Reset all matrices to diagonal (1:1) mapping
    void resetToDefaults();

    // Get routing configuration
    bool getInputRouting(int juceChannel, int jsfxChannel) const;
    bool getSidechainRouting(int juceChannel, int jsfxChannel) const;
    bool getOutputRouting(int jsfxChannel, int juceChannel) const;

    // Calculate appropriate window size based on content and screen
    juce::Rectangle<int> getIdealBounds() const;

    std::function<void()> onRoutingChanged;

private:
    std::unique_ptr<juce::TextButton> resetButton;
    std::unique_ptr<juce::Viewport> viewport;
    std::unique_ptr<IOMatrixContent> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IOMatrixComponent)
};
