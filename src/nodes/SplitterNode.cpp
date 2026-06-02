#include "SplitterNode.h"

SplitterNode::SplitterNode(int numOutputBuses, int numChannels)
    : numOutputBuses_(numOutputBuses), numChannels_(numChannels)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::canonicalChannelSet(numChannels));
    for (int i = 0; i < numOutputBuses; ++i)
        layout.outputBuses.add(juce::AudioChannelSet::canonicalChannelSet(numChannels));
    setBusesLayout(layout);
}

void SplitterNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    int ns = buffer.getNumSamples();
    // Copy input (first numChannels_) to each output bus
    for (int bus = 1; bus < numOutputBuses_; ++bus)
    {
        for (int c = 0; c < numChannels_; ++c)
        {
            int dstCh = bus * numChannels_ + c;
            if (dstCh >= buffer.getNumChannels()) break;
            buffer.copyFrom(dstCh, 0, buffer, c, 0, ns);
        }
    }
}
