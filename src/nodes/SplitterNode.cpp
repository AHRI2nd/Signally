#include "SplitterNode.h"

juce::AudioProcessor::BusesProperties SplitterNode::makeProps(int numOutputBuses, int ch)
{
    BusesProperties bp;
    bp = bp.withInput("Input", juce::AudioChannelSet::canonicalChannelSet(ch), true);
    for (int i = 0; i < numOutputBuses; ++i)
        bp = bp.withOutput("Out" + juce::String(i + 1), juce::AudioChannelSet::canonicalChannelSet(ch), true);
    return bp;
}

SplitterNode::SplitterNode(int numOutputBuses, int numChannels)
    : juce::AudioProcessor(makeProps(numOutputBuses, numChannels)),
      numOutputBuses_(numOutputBuses), numChannels_(numChannels)
{
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
