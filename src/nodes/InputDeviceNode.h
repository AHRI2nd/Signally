#pragma once
#include <JuceHeader.h>
#include "engine/ClockDriftCorrector.h"
#include <mutex>
#include <vector>

// Graph node that feeds captured audio (from a DeviceThread) into the graph.
// DeviceThread calls pushAudio() from its real-time thread;
// the graph calls processBlock() on its mix thread.
// A lock-free ring buffer decouples the two clocks.
class InputDeviceNode : public juce::AudioProcessor
{
public:
    explicit InputDeviceNode(int numChannels);

    // Called by DeviceThread (real-time, non-graph thread)
    void pushAudio(float** channels, int numChannels, int numSamples);

    // Enables on-the-fly resampling from the capture device rate to the engine
    // rate. When the two match, audio passes through untouched. Call before start.
    void setResampling(double deviceRate, double engineRate);

    // ── AudioProcessor interface ──────────────────────────────────────────
    const juce::String getName() const override { return "InputDevice"; }
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
    // Simple power-of-two lock-free ring buffer (planar)
    static constexpr int kRingFrames = 8192;
    std::vector<float>   ring_;       // [channel][frame]
    // 64-bit monotonic positions: never overflow in practice (~6M years @ 48kHz),
    // avoiding the signed-overflow UB a 32-bit counter would hit after ~12 hours.
    std::atomic<long long> writePos_{ 0 };
    std::atomic<long long> readPos_ { 0 };

    // Compensates for drift between the capture device clock (producer) and the
    // mix-thread clock (consumer) by occasionally dropping/holding a frame.
    ClockDriftCorrector  drift_{ kRingFrames / 2 };

    // Sample-rate conversion (device rate → engine rate), applied in pushAudio.
    double deviceRate_  = 48000.0;
    double engineRate_  = 48000.0;
    bool   resampling_  = false;
    std::vector<juce::LagrangeInterpolator> interp_;       // one per channel
    std::vector<std::vector<float>>         rsScratch_;    // per-channel engine-rate output
};
