#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>

// Thin wrapper around AudioProcessorGraph that owns the graph lifecycle,
// exposes typed node IDs, and serialises graph state to/from JSON.
class MixingGraph
{
public:
    using NodeID = juce::AudioProcessorGraph::NodeID;

    MixingGraph();
    ~MixingGraph();

    // Prepare / release audio resources
    void prepare(double sampleRate, int blockSize);
    void release();

    // Topology
    NodeID addNode(std::unique_ptr<juce::AudioProcessor> processor);
    bool   removeNode(NodeID id);
    bool   connect(NodeID srcNode, int srcChannel, NodeID dstNode, int dstChannel);
    bool   disconnect(NodeID srcNode, int srcChannel, NodeID dstNode, int dstChannel);
    void   disconnectAll(NodeID id);

    // Process one block. Called from the engine's mix thread.
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    // Serialisation
    juce::ValueTree toValueTree() const;
    void            fromValueTree(const juce::ValueTree& tree,
                                  std::function<std::unique_ptr<juce::AudioProcessor>(const juce::ValueTree&)> factory);

    juce::AudioProcessorGraph& graph() { return graph_; }

private:
    juce::AudioProcessorGraph graph_;
    double sampleRate_ = 48000.0;
    int    blockSize_  = 512;
};
