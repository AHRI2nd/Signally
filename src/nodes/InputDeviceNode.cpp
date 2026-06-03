#include "InputDeviceNode.h"

InputDeviceNode::InputDeviceNode(int numChannels)
    : numChannels_(numChannels)
{
    ring_.assign(static_cast<size_t>(numChannels * kRingFrames), 0.0f);
    // An input device is a graph SOURCE — it must expose OUTPUT channels (and no
    // inputs) so downstream nodes can connect to it. (Previously this was set as
    // an input bus, leaving the node with 0 output channels → connections failed.)
    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add(juce::AudioChannelSet::canonicalChannelSet(numChannels));
    setBusesLayout(layout);
}

void InputDeviceNode::prepareToPlay(double, int blockSize)
{
    // Aim to keep ~two blocks buffered: low latency, yet enough slack for jitter.
    const int target = juce::jmin(blockSize * 2, kRingFrames / 2);
    drift_.setTargetFill(target);
    writePos_.store(0, std::memory_order_relaxed);
    readPos_ .store(0, std::memory_order_relaxed);
}

void InputDeviceNode::setResampling(double deviceRate, double engineRate)
{
    deviceRate_ = deviceRate;
    engineRate_ = engineRate;
    resampling_ = (deviceRate > 0.0 && engineRate > 0.0
                   && std::abs(deviceRate - engineRate) > 1.0e-6);

    interp_.clear();
    rsScratch_.clear();
    if (resampling_)
    {
        interp_.resize(static_cast<size_t>(numChannels_));
        for (auto& it : interp_) it.reset();
        rsScratch_.resize(static_cast<size_t>(numChannels_));
    }
}

void InputDeviceNode::pushAudio(float** channels, int numChannels, int numSamples)
{
    const int ch = juce::jmin(numChannels, numChannels_);

    // Producer (device thread) owns writePos_; it only reads readPos_.
    long long       wp = writePos_.load(std::memory_order_relaxed);
    const long long rp = readPos_ .load(std::memory_order_acquire);

    if (resampling_ && ch > 0)
    {
        // Convert device-rate input → engine-rate, then write to the ring.
        const double speedRatio = deviceRate_ / engineRate_;          // input/output
        const int    outCount   = (int) std::floor((double) numSamples / speedRatio);
        if (outCount <= 0) return;

        for (int c = 0; c < ch; ++c)
        {
            if ((int) rsScratch_[c].size() < outCount)
                rsScratch_[c].assign(static_cast<size_t>(outCount), 0.0f);
            interp_[c].process(speedRatio, channels[c], rsScratch_[c].data(), outCount);
        }

        for (int i = 0; i < outCount; ++i)
        {
            if (wp - rp >= kRingFrames) break;
            const int idx = static_cast<int>(wp % kRingFrames);
            for (int c = 0; c < ch; ++c)
                ring_[c * kRingFrames + idx] = rsScratch_[c][static_cast<size_t>(i)];
            ++wp;
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (wp - rp >= kRingFrames)
                break; // ring full — drop remaining incoming frames rather than corrupt unread data

            const int idx = static_cast<int>(wp % kRingFrames);
            for (int c = 0; c < ch; ++c)
                ring_[c * kRingFrames + idx] = channels[c][i];
            ++wp;
        }
    }

    writePos_.store(wp, std::memory_order_release);
}

void InputDeviceNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();

    // Consumer (mix thread) owns readPos_.
    const long long wp = writePos_.load(std::memory_order_acquire);
    long long       rp = readPos_ .load(std::memory_order_relaxed);
    long long       available = wp - rp;

    // Once-per-block clock-drift correction against the target fill level.
    const int corr = drift_.getCorrection(static_cast<int>(available));
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
        const int idx = static_cast<int>(((rp % kRingFrames) + kRingFrames) % kRingFrames);
        for (int c = 0; c < numChannels_; ++c)
            buffer.setSample(c, i, (available > 0) ? ring_[c * kRingFrames + idx] : 0.0f);

        if (available > 0) { ++rp; --available; }
    }

    readPos_.store(rp, std::memory_order_release);
}
