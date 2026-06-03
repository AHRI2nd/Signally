#pragma once
#include <JuceHeader.h>
#include <functional>

class GraphEditorComponent;

// A draggable node box in the graph editor.
// Displays a header with the node name, input pins on the left, output pins on the right.
class NodeComponent : public juce::Component
{
public:
    struct Pin
    {
        int  channelIndex;
        bool isInput;
        juce::Point<float> getPosition(const NodeComponent& node) const;
    };

    using NodeID = juce::AudioProcessorGraph::NodeID;

    NodeComponent(GraphEditorComponent& editor,
                  NodeID nodeId,
                  const juce::String& name,
                  int numInputs,
                  int numOutputs);

    NodeID  nodeId() const { return nodeId_; }

    int  numInputs()  const { return numInputs_; }
    int  numOutputs() const { return numOutputs_; }

    juce::Point<float> getPinPosition(int channelIndex, bool isInput) const;

    // Callbacks set by GraphEditorComponent
    std::function<void(NodeComponent*, int, bool, juce::Point<float>)> onPinDragStart;
    std::function<void(NodeComponent*, int, bool, juce::Point<float>)> onPinDragEnd;
    std::function<void(NodeID)>                                         onContextMenu;

    static constexpr int kPinRadius   = 6;
    static constexpr int kHeaderHeight = 26;
    static constexpr int kPinSpacing   = 22;
    static constexpr int kMinWidth     = 150;

protected:
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    int pinHitTest(juce::Point<int> pos, bool& isInput) const;

    GraphEditorComponent& editor_;
    NodeID                nodeId_;
    juce::String          name_;
    int                   numInputs_;
    int                   numOutputs_;

    juce::ComponentDragger dragger_;
    juce::ComponentBoundsConstrainer constrainer_;

    int  draggingPin_      = -1;
    bool draggingIsInput_  = false;
};
