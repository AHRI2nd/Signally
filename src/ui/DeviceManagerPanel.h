#pragma once
#include <JuceHeader.h>
#include "engine/AudioEngine.h"
#include <functional>

class GraphEditorComponent;

// Side panel: shows available input/output devices.
// Double-click or press Enter to add to the graph.
class DeviceManagerPanel : public juce::Component
{
public:
    DeviceManagerPanel(AudioEngine& engine, GraphEditorComponent& editor);

    void refresh();

protected:
    void resized() override;

private:
    void populateList(DeviceDirection direction);
    void onAddInput();
    void onAddOutput(IsolationMode mode);

    AudioEngine&           engine_;
    GraphEditorComponent&  editor_;

    juce::Label            inputLabel_{ {}, "Input Devices" };
    juce::Label            outputLabel_{ {}, "Output Devices" };
    juce::ListBox          inputList_;
    juce::ListBox          outputList_;
    juce::TextButton       addInputBtn_   { "Add Input (Exclusive)" };
    juce::TextButton       addOutputSharedBtn_  { "Add Output (Shared)" };
    juce::TextButton       addOutputExclusiveBtn_{ "Add Output (Exclusive)" };
    juce::TextButton       addVirtualMicBtn_{ "Add Virtual Mic" };
    juce::TextButton       refreshBtn_    { "Refresh" };

    std::vector<DeviceInfo> inputDevices_;
    std::vector<DeviceInfo> outputDevices_;

    // ListBox model
    struct DeviceListModel : juce::ListBoxModel
    {
        std::vector<DeviceInfo>* devices = nullptr;
        int getNumRows() override { return devices ? (int)devices->size() : 0; }
        void paintListBoxItem(int row, juce::Graphics& g,
                              int w, int h, bool selected) override
        {
            if (selected) g.fillAll(juce::Colour(0xff2d4a6b));
            g.setColour(juce::Colours::white);
            g.setFont(12.0f);
            if (devices && row < (int)devices->size())
                g.drawText((*devices)[row].name, 4, 0, w - 8, h,
                           juce::Justification::centredLeft);
        }
    };

    DeviceListModel inputModel_, outputModel_;
};
