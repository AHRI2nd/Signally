#include "OutputDeviceNode.h"

OutputDeviceNode::OutputDeviceNode(int numChannels)
    : numChannels_(numChannels)
{
    ring_.assign(static_cast<size_t>(numChannels * kRingFrames), 0.0f);
    setBusesLayout({ juce::AudioChannelSet::disabled(),
                     juce::AudioChannelSet::canonicalChannelSet(numChannels) });
}

void OutputDeviceNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    int numSamples = buffer.getNumSamples();
    for (int i = 0; i < numSamples; ++i)
    {
        int wp = writePos_.load(std::memory_order_relaxed) % kRingFrames;
        for (int c = 0; c < numChannels_; ++c)
            ring_[c * kRingFrames + wp] = buffer.getSample(c, i);
        writePos_.fetch_add(1, std::memory_order_release);
    }
}

void OutputDeviceNode::pullAudio(float** channels, int numChannels, int numSamples)
{
    int ch        = std::min(numChannels, numChannels_);
    int available = writePos_.load(std::memory_order_acquire)
                  - readPos_.load(std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        int rp = readPos_.load(std::memory_order_relaxed) % kRingFrames;
        for (int c = 0; c < ch; ++c)
            channels[c][i] = (available > 0) ? ring_[c * kRingFrames + rp] : 0.0f;

        if (available > 0)
        {
            readPos_.fetch_add(1, std::memory_order_release);
            --available;
        }
    }
}
