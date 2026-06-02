#pragma once
#include <JuceHeader.h>
#include "engine/VirtualMicBridge.h"

// Graph sink node: writes processed audio to the VirtualMicBridge
// (shared memory → kernel driver → Discord sees it as "microphone").
class VirtualMicOutputNode : public juce::AudioProcessor
{
public:
    VirtualMicOutputNode(VirtualMicBridge& bridge, int numChannels);

    const juce::String getName() const override { return "VirtualMic"; }
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

private:
    VirtualMicBridge& bridge_;
    int numChannels_;
};
