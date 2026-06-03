#include "MixerNode.h"

MixerNode::MixerNode(int numInputBuses, int numChannels)
    : numInputBuses_(numInputBuses), numChannels_(numChannels), gains_(numInputBuses)
{
    for (auto& g : gains_) g.store(1.0f);

    juce::AudioProcessor::BusesLayout layout;
    for (int i = 0; i < numInputBuses; ++i)
        layout.inputBuses.add(juce::AudioChannelSet::canonicalChannelSet(numChannels));
    layout.outputBuses.add(juce::AudioChannelSet::canonicalChannelSet(numChannels));
    setBusesLayout(layout);
}

void MixerNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // JUCE AudioProcessorGraph interleaves all buses into one buffer.
    // For a simple implementation: the first numChannels_ channels are the output,
    // additional input channels are summed into the output.
    int ns = buffer.getNumSamples();
    int totalCh = buffer.getNumChannels();

    // Zero output channels
    for (int c = 0; c < numChannels_; ++c)
        buffer.clear(c, 0, ns);

    // Sum each input bus
    for (int bus = 0; bus < numInputBuses_; ++bus)
    {
        float gain = gains_[bus].load(std::memory_order_relaxed);
        for (int c = 0; c < numChannels_; ++c)
        {
            int srcCh = bus * numChannels_ + c;
            if (srcCh >= totalCh) break;
            buffer.addFrom(c, 0, buffer, srcCh, 0, ns, gain);
        }
    }
}

void MixerNode::setBusGain(int busIndex, float gain)
{
    if (busIndex >= 0 && busIndex < numInputBuses_)
        gains_[busIndex].store(gain);
}

float MixerNode::getBusGain(int busIndex) const
{
    if (busIndex >= 0 && busIndex < numInputBuses_)
        return gains_[busIndex].load();
    return 0.0f;
}
