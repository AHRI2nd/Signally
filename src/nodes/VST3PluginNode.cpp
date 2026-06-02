#include "VST3PluginNode.h"

VST3PluginNode::VST3PluginNode(std::unique_ptr<juce::AudioPluginInstance> plugin)
    : plugin_(std::move(plugin))
{
    if (plugin_)
        setBusesLayout(plugin_->getBusesLayout());
}

VST3PluginNode::~VST3PluginNode()
{
    if (plugin_) plugin_->releaseResources();
}

const juce::String VST3PluginNode::getName() const
{
    return plugin_ ? plugin_->getName() : "VST3Plugin";
}

void VST3PluginNode::prepareToPlay(double sampleRate, int blockSize)
{
    if (plugin_) plugin_->prepareToPlay(sampleRate, blockSize);
}

void VST3PluginNode::releaseResources()
{
    if (plugin_) plugin_->releaseResources();
}

void VST3PluginNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (plugin_) plugin_->processBlock(buffer, midi);
}

double VST3PluginNode::getTailLengthSeconds() const
{
    return plugin_ ? plugin_->getTailLengthSeconds() : 0.0;
}

bool VST3PluginNode::acceptsMidi() const  { return plugin_ && plugin_->acceptsMidi(); }
bool VST3PluginNode::producesMidi() const { return plugin_ && plugin_->producesMidi(); }
bool VST3PluginNode::hasEditor() const    { return plugin_ && plugin_->hasEditor(); }

juce::AudioProcessorEditor* VST3PluginNode::createEditor()
{
    return plugin_ ? plugin_->createEditor() : nullptr;
}

int VST3PluginNode::getNumPrograms()    { return plugin_ ? plugin_->getNumPrograms() : 1; }
int VST3PluginNode::getCurrentProgram() { return plugin_ ? plugin_->getCurrentProgram() : 0; }

void VST3PluginNode::setCurrentProgram(int index)
{
    if (plugin_) plugin_->setCurrentProgram(index);
}

const juce::String VST3PluginNode::getProgramName(int index)
{
    return plugin_ ? plugin_->getProgramName(index) : juce::String{};
}

void VST3PluginNode::getStateInformation(juce::MemoryBlock& dest)
{
    if (plugin_) plugin_->getStateInformation(dest);
}

void VST3PluginNode::setStateInformation(const void* data, int size)
{
    if (plugin_) plugin_->setStateInformation(data, size);
}
