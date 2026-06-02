#include "NodeComponent.h"
#include "GraphEditorComponent.h"

static constexpr juce::Colour kHeaderColour  { 0xff2d4a6b };
static constexpr juce::Colour kBodyColour    { 0xff1a2a3a };
static constexpr juce::Colour kPinInputColour { 0xff4fc3f7 };
static constexpr juce::Colour kPinOutputColour{ 0xffffa726 };
static constexpr juce::Colour kBorderColour  { 0xff3d6a9e };

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
    float x = isInput ? 0.0f : static_cast<float>(getWidth());
    float y = static_cast<float>(kHeaderHeight + kPinSpacing * (channelIndex + 1));
    return getPosition().toFloat() + juce::Point<float>(x, y);
}

void NodeComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    // Body
    g.setColour(kBodyColour);
    g.fillRoundedRectangle(bounds, 6.0f);

    // Border
    g.setColour(isMouseOver() ? kBorderColour.brighter(0.3f) : kBorderColour);
    g.drawRoundedRectangle(bounds, 6.0f, 1.5f);

    // Header
    juce::Rectangle<float> header(bounds.getX(), bounds.getY(),
                                   bounds.getWidth(), kHeaderHeight);
    g.setColour(kHeaderColour);
    g.fillRoundedRectangle(header, 6.0f);
    g.fillRect(header.withTrimmedTop(4.0f));

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(name_, header.toNearestInt(), juce::Justification::centred);

    // Input pins
    for (int i = 0; i < numInputs_; ++i)
    {
        auto pos = getPinPosition(i, true) - getPosition().toFloat();
        g.setColour(kPinInputColour);
        g.fillEllipse(pos.x - kPinRadius, pos.y - kPinRadius,
                      kPinRadius * 2, kPinRadius * 2);
        g.setColour(kBorderColour);
        g.drawEllipse(pos.x - kPinRadius, pos.y - kPinRadius,
                      kPinRadius * 2, kPinRadius * 2, 1.0f);
    }

    // Output pins
    for (int i = 0; i < numOutputs_; ++i)
    {
        auto pos = getPinPosition(i, false) - getPosition().toFloat();
        g.setColour(kPinOutputColour);
        g.fillEllipse(pos.x - kPinRadius, pos.y - kPinRadius,
                      kPinRadius * 2, kPinRadius * 2);
        g.setColour(kBorderColour);
        g.drawEllipse(pos.x - kPinRadius, pos.y - kPinRadius,
                      kPinRadius * 2, kPinRadius * 2, 1.0f);
    }
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
    else
    {
        draggingPin_ = -1;
        dragger_.startDraggingComponent(this, e);
        if (e.mods.isRightButtonDown() && onRemoveRequested)
            onRemoveRequested(nodeId_);
    }
}

void NodeComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingPin_ >= 0)
    {
        // The GraphEditorComponent handles the preview line
        editor_.repaint();
    }
    else
    {
        dragger_.dragComponent(this, e, &constrainer_);
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
