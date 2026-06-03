#pragma once
#include <JuceHeader.h>
#include "engine/ClockDriftCorrector.h"
#include <atomic>
#include <vector>

// Graph node that buffers rendered audio for a DeviceThread to consume.
// Graph calls processBlock() → stores samples; DeviceThread calls pullAudio().
class OutputDeviceNode : public juce::AudioProcessor
{
public:
    explicit OutputDeviceNode(int numChannels);

    // Called by DeviceThread (real-time)
    void pullAudio(float** channels, int numChannels, int numSamples);

    // Enables on-the-fly resampling from the engine rate to the render device
    // rate. When the two match, audio passes through untouched. Call before start.
    void setResampling(double deviceRate, double engineRate);

    // ── AudioProcessor interface ──────────────────────────────────────────
    const juce::String getName() const override { return "OutputDevice"; }
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    double getTailLengthSeconds() const override { return 0.0; }
    bool   acceptsMidi() const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    int numChannels_;
    static constexpr int kRingFrames = 8192;
    std::vector<float>   ring_;
    // 64-bit monotonic positions (see InputDeviceNode) — avoid 32-bit overflow UB.
    std::atomic<long long> writePos_{ 0 };
    std::atomic<long long> readPos_ { 0 };

    // Compensates for drift between the mix-thread clock (producer) and the
    // render device clock (consumer, pullAudio) by dropping/holding a frame.
    ClockDriftCorrector  drift_{ kRingFrames / 2 };

    // Sample-rate conversion (engine rate → device rate), applied in pullAudio.
    double deviceRate_  = 48000.0;
    double engineRate_  = 48000.0;
    bool   resampling_  = false;
    std::vector<juce::LagrangeInterpolator> interp_;    // one per channel
    std::vector<std::vector<float>>         rsIn_;      // per-channel engine-rate input
};
