#include "NodeComponent.h"
#include "GraphEditorComponent.h"

static const juce::Colour kHeaderColour  { 0xff363d4a };  // slate header
static const juce::Colour kBodyColour    { 0xff282d36 };  // slate body
static const juce::Colour kPinInputColour { 0xff4ec9b0 };  // teal (inputs)
static const juce::Colour kPinOutputColour{ 0xff6ea8fe };  // blue (outputs)
static const juce::Colour kBorderColour  { 0xff404857 };

NodeComponent::NodeComponent(GraphEditorComponent& editor,
                             NodeID nodeId,
                             const juce::String& name,
                             int numInputs,
                             int numOutputs)
    : editor_(editor), nodeId_(nodeId), name_(name),
      numInputs_(numInputs), numOutputs_(numOutputs)
{
    int height = kHeaderHeight + kPinSpacing * (std::max(numInputs, numOutputs) + 1);
    setSize(kMinWidth, height);
    setRepaintsOnMouseActivity(true);
}

juce::Point<float> NodeComponent::getPinPosition(int channelIndex, bool isInput) const
{
    // Inset the pins slightly so their full hit area is inside the node bounds
    // (a pin on x==0/width would have half its circle outside the component and
    // would not receive mouse clicks).
    float x = isInput ? (kPinRadius + 2.0f)
                      : static_cast<float>(getWidth()) - (kPinRadius + 2.0f);
    float y = static_cast<float>(kHeaderHeight + kPinSpacing * (channelIndex + 1));
    return getPosition().toFloat() + juce::Point<float>(x, y);
}

void NodeComponent::paint(juce::Graphics& g)
{
    auto       bounds = getLocalBounds().toFloat().reduced(2.0f);
    const float radius = 8.0f;
    const bool  hov    = isMouseOver(true);

    // Drop shadow
    g.setColour(juce::Colours::black.withAlpha(0.40f));
    g.fillRoundedRectangle(bounds.translated(0.0f, 2.5f), radius);

    // Body gradient
    g.setGradientFill(juce::ColourGradient(
        kBodyColour.brighter(0.07f), bounds.getTopLeft(),
        kBodyColour.darker(0.18f),  bounds.getBottomLeft(), false));
    g.fillRoundedRectangle(bounds, radius);

    // Header gradient (top-rounded only)
    juce::Rectangle<float> header(bounds.getX(), bounds.getY(), bounds.getWidth(), (float) kHeaderHeight);
    juce::Path hp;
    hp.addRoundedRectangle(header.getX(), header.getY(), header.getWidth(), header.getHeight(),
                           radius, radius, true, true, false, false);
    g.setGradientFill(juce::ColourGradient(
        kHeaderColour.brighter(0.12f), header.getTopLeft(),
        kHeaderColour.darker(0.10f),  header.getBottomLeft(), false));
    g.fillPath(hp);

    // Border (accent when hovered)
    g.setColour(hov ? kPinOutputColour.withAlpha(0.85f) : kBorderColour);
    g.drawRoundedRectangle(bounds, radius, 1.5f);

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(12.5f, juce::Font::bold));
    g.drawText(name_, header.toNearestInt().reduced(10, 0), juce::Justification::centredLeft);

    // Pins (+ channel index labels)
    auto drawPin = [&](int i, bool isInput)
    {
        auto  pos = getPinPosition(i, isInput) - getPosition().toFloat();
        auto  col = isInput ? kPinInputColour : kPinOutputColour;
        float r   = kPinRadius + (hov ? 1.0f : 0.0f);

        g.setColour(col);
        g.fillEllipse(pos.x - r, pos.y - r, r * 2, r * 2);
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.drawEllipse(pos.x - r, pos.y - r, r * 2, r * 2, 1.2f);

        g.setColour(juce::Colours::lightgrey);
        g.setFont(9.0f);
        if (isInput)
            g.drawText(juce::String(i), (int) pos.x + 9, (int) pos.y - 7, 22, 14, juce::Justification::left);
        else
            g.drawText(juce::String(i), (int) pos.x - 31, (int) pos.y - 7, 22, 14, juce::Justification::right);
    };
    for (int i = 0; i < numInputs_;  ++i) drawPin(i, true);
    for (int i = 0; i < numOutputs_; ++i) drawPin(i, false);
}

int NodeComponent::pinHitTest(juce::Point<int> pos, bool& isInput) const
{
    for (int i = 0; i < numInputs_; ++i)
    {
        auto p = getPinPosition(i, true) - getPosition().toFloat();
        if (pos.toFloat().getDistanceFrom(p) <= kPinRadius + 3)
        {
            isInput = true;
            return i;
        }
    }
    for (int i = 0; i < numOutputs_; ++i)
    {
        auto p = getPinPosition(i, false) - getPosition().toFloat();
        if (pos.toFloat().getDistanceFrom(p) <= kPinRadius + 3)
        {
            isInput = false;
            return i;
        }
    }
    return -1;
}

void NodeComponent::mouseDown(const juce::MouseEvent& e)
{
    bool isInput = false;
    int pin = pinHitTest(e.getPosition(), isInput);
    if (pin >= 0)
    {
        draggingPin_     = pin;
        draggingIsInput_ = isInput;
        if (onPinDragStart)
            onPinDragStart(this, pin, isInput, getPinPosition(pin, isInput));
    }
    else if (e.mods.isRightButtonDown())
    {
        draggingPin_ = -1;
        if (onContextMenu) onContextMenu(nodeId_);
    }
    else
    {
        draggingPin_ = -1;
        dragger_.startDraggingComponent(this, e);
    }
}

void NodeComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingPin_ >= 0)
    {
        // Feed the live mouse position so the editor draws the preview line.
        editor_.updatePendingDrag(getPosition().toFloat() + e.position);
    }
    else
    {
        dragger_.dragComponent(this, e, &constrainer_);
        editor_.updateCanvasBounds();
        editor_.repaint();
    }
}

void NodeComponent::mouseUp(const juce::MouseEvent& e)
{
    if (draggingPin_ >= 0 && onPinDragEnd)
    {
        onPinDragEnd(this, draggingPin_, draggingIsInput_,
                     getPosition().toFloat() + e.position);
        draggingPin_ = -1;
    }
}
