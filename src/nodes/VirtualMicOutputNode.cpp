#include "VirtualMicOutputNode.h"

VirtualMicOutputNode::VirtualMicOutputNode(VirtualMicBridge& bridge, int numChannels)
    : bridge_(bridge), numChannels_(numChannels)
{
    setBusesLayout({ juce::AudioChannelSet::canonicalChannelSet(numChannels),
                     juce::AudioChannelSet::disabled() });
}

void VirtualMicOutputNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Build channel pointer array for VirtualMicBridge
    int ch = std::min(buffer.getNumChannels(), numChannels_);
    std::vector<float*> ptrs(ch);
    for (int c = 0; c < ch; ++c)
        ptrs[c] = const_cast<float*>(buffer.getReadPointer(c));

    bridge_.write(ptrs.data(), ch, buffer.getNumSamples());
}
