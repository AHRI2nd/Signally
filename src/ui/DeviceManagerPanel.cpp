#include "DeviceManagerPanel.h"
#include "GraphEditorComponent.h"

DeviceManagerPanel::DeviceManagerPanel(AudioEngine& engine, GraphEditorComponent& editor)
    : engine_(engine), editor_(editor)
{
    inputModel_.devices  = &inputDevices_;
    outputModel_.devices = &outputDevices_;

    inputList_.setModel(&inputModel_);
    outputList_.setModel(&outputModel_);

    for (auto* c : { &inputLabel_, &outputLabel_ })
    {
        c->setFont(juce::Font(13.0f, juce::Font::bold));
        c->setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(c);
    }

    addAndMakeVisible(inputList_);
    addAndMakeVisible(outputList_);

    auto setupBtn = [this](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d3557));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(btn);
    };
    setupBtn(addInputBtn_);
    setupBtn(addOutputSharedBtn_);
    setupBtn(addOutputExclusiveBtn_);
    setupBtn(addVirtualMicBtn_);
    setupBtn(refreshBtn_);

    addInputBtn_.onClick = [this] { onAddInput(); };
    addOutputSharedBtn_.onClick  = [this] { onAddOutput(IsolationMode::Shared);    };
    addOutputExclusiveBtn_.onClick = [this] { onAddOutput(IsolationMode::Exclusive); };
    addVirtualMicBtn_.onClick = [this] { editor_.addVirtualMicNode(); };
    refreshBtn_.onClick = [this] { refresh(); };

    refresh();
}

void DeviceManagerPanel::refresh()
{
    inputDevices_  = engine_.enumerateDevices(DeviceDirection::Input);
    outputDevices_ = engine_.enumerateDevices(DeviceDirection::Output);
    inputList_.updateContent();
    outputList_.updateContent();
}

void DeviceManagerPanel::resized()
{
    auto area = getLocalBounds().reduced(6);
    auto row = [&](int h) { return area.removeFromTop(h); };

    inputLabel_.setBounds(row(18));
    inputList_ .setBounds(row(140));
    addInputBtn_.setBounds(row(26).reduced(0, 2));

    area.removeFromTop(8);
    outputLabel_.setBounds(row(18));
    outputList_ .setBounds(row(140));
    addOutputSharedBtn_  .setBounds(row(26).reduced(0, 2));
    addOutputExclusiveBtn_.setBounds(row(26).reduced(0, 2));

    area.removeFromTop(8);
    addVirtualMicBtn_.setBounds(row(26).reduced(0, 2));
    refreshBtn_      .setBounds(row(26).reduced(0, 2));
}

void DeviceManagerPanel::onAddInput()
{
    int row = inputList_.getSelectedRow();
    if (row < 0 || row >= (int)inputDevices_.size()) return;
    editor_.addInputDeviceNode(inputDevices_[row]);
}

void DeviceManagerPanel::onAddOutput(IsolationMode mode)
{
    int row = outputList_.getSelectedRow();
    if (row < 0 || row >= (int)outputDevices_.size()) return;
    editor_.addOutputDeviceNode(outputDevices_[row], mode);
}
