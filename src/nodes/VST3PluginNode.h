#pragma once
#include <JuceHeader.h>
#include <memory>

// Thin wrapper that lets a loaded VST3 AudioPluginInstance participate in
// the MixingGraph. The plugin instance is owned by this node.
class VST3PluginNode : public juce::AudioProcessor
{
public:
    explicit VST3PluginNode(std::unique_ptr<juce::AudioPluginInstance> plugin);
    ~VST3PluginNode() override;

    juce::AudioPluginInstance* getPlugin() const { return plugin_.get(); }

    // ── AudioProcessor ──────────────────────────────────────────────────
    const juce::String getName() const override;
    void prepareToPlay(double sampleRate, int blockSize) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    double getTailLengthSeconds() const override;
    bool   acceptsMidi() const override;
    bool   producesMidi() const override;
    bool   hasEditor() const override;
    juce::AudioProcessorEditor* createEditor() override;

    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin_;
};
