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

juce::ValueTree MixingGraph::toValueTree() const
{
    juce::ValueTree state("MixingGraph");
    // Nodes
    juce::ValueTree nodes("Nodes");
    for (auto* node : graph_.getNodes())
    {
        juce::ValueTree n("Node");
        n.setProperty("id",   (int)node->nodeID.uid, nullptr);
        n.setProperty("type", node->getProcessor()->getName(), nullptr);
        juce::MemoryBlock mb;
        node->getProcessor()->getStateInformation(mb);
        n.setProperty("state", mb.toBase64Encoding(), nullptr);
        nodes.appendChild(n, nullptr);
    }
    state.appendChild(nodes, nullptr);
    // Connections
    juce::ValueTree conns("Connections");
    for (auto& c : graph_.getConnections())
    {
        juce::ValueTree cn("Connection");
        cn.setProperty("srcNode",    (int)c.source.nodeID.uid,      nullptr);
        cn.setProperty("srcChannel", c.source.channelIndex,         nullptr);
        cn.setProperty("dstNode",    (int)c.destination.nodeID.uid, nullptr);
        cn.setProperty("dstChannel", c.destination.channelIndex,    nullptr);
        conns.appendChild(cn, nullptr);
    }
    state.appendChild(conns, nullptr);
    return state;
}

void MixingGraph::fromValueTree(
    const juce::ValueTree& tree,
    std::function<std::unique_ptr<juce::AudioProcessor>(const juce::ValueTree&)> factory)
{
    graph_.clear();

    auto nodes = tree.getChildWithName("Nodes");
    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        auto n  = nodes.getChild(i);
        auto p  = factory(n);
        if (!p) continue;

        juce::MemoryBlock mb;
        mb.fromBase64Encoding(n.getProperty("state").toString());
        p->setStateInformation(mb.getData(), (int)mb.getSize());
        addNode(std::move(p));
    }

    // Connections restored by ID — a real impl maps saved IDs to new NodeIDs.
    // Kept simple for the initial version.
}
