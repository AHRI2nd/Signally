#include "InputDeviceNode.h"

InputDeviceNode::InputDeviceNode(int numChannels)
    : numChannels_(numChannels)
{
    ring_.assign(static_cast<size_t>(numChannels * kRingFrames), 0.0f);
    setBusesLayout({ juce::AudioChannelSet::canonicalChannelSet(numChannels),
                     juce::AudioChannelSet::disabled() });
}

void InputDeviceNode::prepareToPlay(double, int blockSize)
{
    // Aim to keep ~two blocks buffered: low latency, yet enough slack for jitter.
    const int target = juce::jmin(blockSize * 2, kRingFrames / 2);
    drift_.setTargetFill(target);
    writePos_.store(0, std::memory_order_relaxed);
    readPos_ .store(0, std::memory_order_relaxed);
}

void InputDeviceNode::pushAudio(float** channels, int numChannels, int numSamples)
{
    const int ch = juce::jmin(numChannels, numChannels_);

    // Producer (device thread) owns writePos_; it only reads readPos_.
    int       wp = writePos_.load(std::memory_order_relaxed);
    const int rp = readPos_ .load(std::memory_order_acquire);

    for (int i = 0; i < numSamples; ++i)
    {
        if (wp - rp >= kRingFrames)
            break; // ring full — drop remaining incoming frames rather than corrupt unread data

        const int idx = wp % kRingFrames;
        for (int c = 0; c < ch; ++c)
            ring_[c * kRingFrames + idx] = channels[c][i];
        ++wp;
    }

    writePos_.store(wp, std::memory_order_release);
}

void InputDeviceNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();

    // Consumer (mix thread) owns readPos_.
    const int wp = writePos_.load(std::memory_order_acquire);
    int       rp = readPos_ .load(std::memory_order_relaxed);
    int       available = wp - rp;

    // Once-per-block clock-drift correction against the target fill level.
    const int corr = drift_.getCorrection(available);
    if (corr < 0 && available > numSamples)
    {
        ++rp; --available;   // drop one captured frame → shed accumulated latency
    }
    else if (corr > 0)
    {
        --rp; ++available;   // re-serve one frame → refill a draining buffer
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const int idx = ((rp % kRingFrames) + kRingFrames) % kRingFrames;
        for (int c = 0; c < numChannels_; ++c)
            buffer.setSample(c, i, (available > 0) ? ring_[c * kRingFrames + idx] : 0.0f);

        if (available > 0) { ++rp; --available; }
    }

    readPos_.store(rp, std::memory_order_release);
}
