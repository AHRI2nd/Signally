#pragma once
#include <JuceHeader.h>
#include <functional>

class GraphEditorComponent;

// Panel for scanning and loading VST3 plugins into the graph.
class VST3BrowserPanel : public juce::Component, private juce::ChangeListener
{
public:
    VST3BrowserPanel(GraphEditorComponent& editor);
    ~VST3BrowserPanel() override;

protected:
    void resized() override;

private:
    void scanPlugins();
    void onAddPlugin();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    GraphEditorComponent&         editor_;
    juce::AudioPluginFormatManager formatManager_;
    juce::KnownPluginList          knownList_;

    juce::Label       titleLabel_{ {}, "VST3 Plugins" };
    juce::ListBox     pluginList_;
    juce::TextButton  scanBtn_  { "Scan VST3 Folders" };
    juce::TextButton  addBtn_   { "Add to Graph" };
    juce::Label       statusLabel_;

    struct PluginListModel : juce::ListBoxModel
    {
        juce::KnownPluginList* list = nullptr;
        int getNumRows() override
        {
            return list ? list->getNumTypes() : 0;
        }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override
        {
            if (sel) g.fillAll(juce::Colour(0xff2d4a6b));
            g.setColour(juce::Colours::white);
            g.setFont(11.0f);
            if (list && row < list->getNumTypes())
                g.drawText(list->getTypes()[row].name, 4, 0, w - 8, h,
                           juce::Justification::centredLeft);
        }
    } listModel_;
};
