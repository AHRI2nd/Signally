#include "MixingGraph.h"

MixingGraph::MixingGraph()
{
    graph_.setPlayConfigDetails(0, 0, 48000.0, 512);
}

MixingGraph::~MixingGraph()
{
    release();
}

void MixingGraph::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_  = blockSize;
    graph_.setPlayConfigDetails(0, 0, sampleRate, blockSize);
    graph_.prepareToPlay(sampleRate, blockSize);
}

void MixingGraph::release()
{
    graph_.releaseResources();
}

MixingGraph::NodeID MixingGraph::addNode(std::unique_ptr<juce::AudioProcessor> processor)
{
    auto* node = graph_.addNode(std::move(processor)).get();
    return node ? node->nodeID : juce::AudioProcessorGraph::NodeID{};
}

bool MixingGraph::removeNode(NodeID id)
{
    return graph_.removeNode(id);
}

bool MixingGraph::connect(NodeID src, int srcCh, NodeID dst, int dstCh)
{
    return graph_.addConnection({ { src, srcCh }, { dst, dstCh } });
}

bool MixingGraph::disconnect(NodeID src, int srcCh, NodeID dst, int dstCh)
{
    return graph_.removeConnection({ { src, srcCh }, { dst, dstCh } });
}

void MixingGraph::disconnectAll(NodeID id)
{
    graph_.disconnectNode(id);
}

void MixingGraph::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    graph_.processBlock(buffer, midi);
}
