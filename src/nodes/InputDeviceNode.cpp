#include "InputDeviceNode.h"

InputDeviceNode::InputDeviceNode(int numChannels)
    : numChannels_(numChannels)
{
    ring_.assign(static_cast<size_t>(numChannels * kRingFrames), 0.0f);
    setBusesLayout({ juce::AudioChannelSet::canonicalChannelSet(numChannels),
                     juce::AudioChannelSet::disabled() });
}

void InputDeviceNode::pushAudio(float** channels, int numChannels, int numSamples)
{
    int ch = std::min(numChannels, numChannels_);
    for (int i = 0; i < numSamples; ++i)
    {
        int wp = writePos_.load(std::memory_order_relaxed) % kRingFrames;
        for (int c = 0; c < ch; ++c)
            ring_[c * kRingFrames + wp] = channels[c][i];
        writePos_.fetch_add(1, std::memory_order_release);
    }
}

void InputDeviceNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    int numSamples = buffer.getNumSamples();
    int available  = writePos_.load(std::memory_order_acquire)
                   - readPos_.load(std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        int rp = readPos_.load(std::memory_order_relaxed) % kRingFrames;
        for (int c = 0; c < numChannels_; ++c)
        {
            float sample = (available > 0) ? ring_[c * kRingFrames + rp] : 0.0f;
            buffer.setSample(c, i, sample);
        }
        if (available > 0)
        {
            readPos_.fetch_add(1, std::memory_order_release);
            --available;
        }
    }
}
