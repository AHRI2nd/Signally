#pragma once
#include <JuceHeader.h>
#include "engine/AudioEngine.h"
#include "ui/GraphEditorComponent.h"
#include "ui/DeviceManagerPanel.h"
#include "ui/VST3BrowserPanel.h"

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow();
    ~MainWindow() override;

    void closeButtonPressed() override;

private:
    class Content : public juce::Component,
                    private juce::Timer
    {
    public:
        Content();
        ~Content() override;

        void resized() override;
        void paint(juce::Graphics& g) override;

        bool startEngine();
        void stopEngine();

    private:
        void timerCallback() override;

        AudioEngine            engine_;
        juce::Viewport         graphViewport_;
        GraphEditorComponent   graphEditor_;
        DeviceManagerPanel     devicePanel_;
        VST3BrowserPanel       vst3Panel_;

        juce::TextButton       startBtn_{ "Start Engine" };
        juce::TextButton       stopBtn_ { "Stop Engine" };
        juce::TextButton       saveBtn_ { "Save" };
        juce::TextButton       loadBtn_ { "Load" };
        juce::Label            statusLabel_;
        std::unique_ptr<juce::FileChooser> chooser_;
        bool                   engineRunning_ = false;
    };

    std::unique_ptr<Content> content_;
};
