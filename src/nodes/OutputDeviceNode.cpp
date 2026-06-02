#include "OutputDeviceNode.h"

OutputDeviceNode::OutputDeviceNode(int numChannels)
    : numChannels_(numChannels)
{
    ring_.assign(static_cast<size_t>(numChannels * kRingFrames), 0.0f);
    setBusesLayout({ juce::AudioChannelSet::disabled(),
                     juce::AudioChannelSet::canonicalChannelSet(numChannels) });
}

void OutputDeviceNode::prepareToPlay(double, int blockSize)
{
    const int target = juce::jmin(blockSize * 2, kRingFrames / 2);
    drift_.setTargetFill(target);
    writePos_.store(0, std::memory_order_relaxed);
    readPos_ .store(0, std::memory_order_relaxed);
}

void OutputDeviceNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();

    // Producer (mix thread) owns writePos_; it only reads readPos_.
    int       wp = writePos_.load(std::memory_order_relaxed);
    const int rp = readPos_ .load(std::memory_order_acquire);

    for (int i = 0; i < numSamples; ++i)
    {
        if (wp - rp >= kRingFrames)
            break; // ring full — device hasn't drained yet; drop rather than overwrite

        const int idx = wp % kRingFrames;
        for (int c = 0; c < numChannels_; ++c)
            ring_[c * kRingFrames + idx] = buffer.getSample(c, i);
        ++wp;
    }

    writePos_.store(wp, std::memory_order_release);
}

void OutputDeviceNode::pullAudio(float** channels, int numChannels, int numSamples)
{
    const int ch = juce::jmin(numChannels, numChannels_);

    // Consumer (device thread) owns readPos_.
    const int wp = writePos_.load(std::memory_order_acquire);
    int       rp = readPos_ .load(std::memory_order_relaxed);
    int       available = wp - rp;

    // Once-per-callback clock-drift correction against the target fill level.
    const int corr = drift_.getCorrection(available);
    if (corr < 0 && available > numSamples)
    {
        ++rp; --available;   // drop one rendered frame → shed accumulated latency
    }
    else if (corr > 0)
    {
        --rp; ++available;   // re-serve one frame → refill a draining buffer
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const int idx = ((rp % kRingFrames) + kRingFrames) % kRingFrames;
        for (int c = 0; c < ch; ++c)
            channels[c][i] = (available > 0) ? ring_[c * kRingFrames + idx] : 0.0f;

        if (available > 0) { ++rp; --available; }
    }

    readPos_.store(rp, std::memory_order_release);
}
