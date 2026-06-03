#pragma once
#include <JuceHeader.h>

// N-to-1 summing mixer: sums all input channels to a single stereo output.
// Graph connections determine which upstream nodes feed into this mixer.
class MixerNode : public juce::AudioProcessor
{
public:
    explicit MixerNode(int numInputBuses, int numChannels = 2);

    const juce::String getName() const override { return "Mixer"; }
    void prepareToPlay(double, int) override {}
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

    // Per-bus gain (0.0–1.0)
    void setBusGain(int busIndex, float gain);
    float getBusGain(int busIndex) const;

private:
    static BusesProperties makeProps(int numInputBuses, int numChannels);

    int numInputBuses_;
    int numChannels_;
    std::vector<std::atomic<float>> gains_;
};
