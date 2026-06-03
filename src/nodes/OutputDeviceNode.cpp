#include "OutputDeviceNode.h"

// Output device is a graph SINK → it exposes an INPUT bus (declared here).
OutputDeviceNode::OutputDeviceNode(int numChannels)
    : juce::AudioProcessor(BusesProperties().withInput(
          "Input", juce::AudioChannelSet::canonicalChannelSet(numChannels), true)),
      numChannels_(numChannels)
{
    ring_.assign(static_cast<size_t>(numChannels * kRingFrames), 0.0f);
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
    long long       wp = writePos_.load(std::memory_order_relaxed);
    const long long rp = readPos_ .load(std::memory_order_acquire);

    for (int i = 0; i < numSamples; ++i)
    {
        if (wp - rp >= kRingFrames)
            break; // ring full — device hasn't drained yet; drop rather than overwrite

        const int idx = static_cast<int>(wp % kRingFrames);
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
    const long long wp = writePos_.load(std::memory_order_acquire);
    long long       rp = readPos_ .load(std::memory_order_relaxed);
    long long       available = wp - rp;

    // Once-per-callback clock-drift correction against the target fill level
    // (measured in engine-rate frames in the ring).
    const int corr = drift_.getCorrection(static_cast<int>(available));
    if (corr < 0 && available > numSamples)
    {
        ++rp; --available;   // drop one rendered frame → shed accumulated latency
    }
    else if (corr > 0)
    {
        --rp; ++available;   // re-serve one frame → refill a draining buffer
    }

    if (resampling_ && ch > 0)
    {
        // Convert engine-rate ring contents → device-rate output samples.
        const double speedRatio = engineRate_ / deviceRate_;          // input/output
        const int    inNeeded   = (int) std::ceil((double) numSamples * speedRatio) + 2;

        if ((int) rsIn_.size() < ch) rsIn_.resize(static_cast<size_t>(ch));
        for (int c = 0; c < ch; ++c)
            if ((int) rsIn_[c].size() < inNeeded)
                rsIn_[c].assign(static_cast<size_t>(inNeeded), 0.0f);

        // Peek inNeeded engine-rate frames (silence on underrun) without committing rp.
        long long lp = rp, avail = available;
        for (int i = 0; i < inNeeded; ++i)
        {
            const int  idx  = static_cast<int>(((lp % kRingFrames) + kRingFrames) % kRingFrames);
            const bool have = (avail > 0);
            for (int c = 0; c < ch; ++c)
                rsIn_[c][static_cast<size_t>(i)] = have ? ring_[c * kRingFrames + idx] : 0.0f;
            if (have) { ++lp; --avail; }
        }

        int used = 0;
        for (int c = 0; c < ch; ++c)
            used = interp_[c].process(speedRatio, rsIn_[c].data(), channels[c], numSamples);

        for (int c = ch; c < numChannels; ++c)
            if (channels[c] != nullptr)
                juce::FloatVectorOperations::clear(channels[c], numSamples);

        // Commit only the input actually consumed, never past the written data.
        const long long consumed = juce::jmin(static_cast<long long>(used),
                                              available > 0 ? available : 0LL);
        readPos_.store(rp + consumed, std::memory_order_release);
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const int idx = static_cast<int>(((rp % kRingFrames) + kRingFrames) % kRingFrames);
        for (int c = 0; c < ch; ++c)
            channels[c][i] = (available > 0) ? ring_[c * kRingFrames + idx] : 0.0f;

        if (available > 0) { ++rp; --available; }
    }

    readPos_.store(rp, std::memory_order_release);
}

void OutputDeviceNode::setResampling(double deviceRate, double engineRate)
{
    deviceRate_ = deviceRate;
    engineRate_ = engineRate;
    resampling_ = (deviceRate > 0.0 && engineRate > 0.0
                   && std::abs(deviceRate - engineRate) > 1.0e-6);

    interp_.clear();
    rsIn_.clear();
    if (resampling_)
    {
        interp_.resize(static_cast<size_t>(numChannels_));
        for (auto& it : interp_) it.reset();
        rsIn_.resize(static_cast<size_t>(numChannels_));
    }
}
