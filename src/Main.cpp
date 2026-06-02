#include <JuceHeader.h>
#include "MainWindow.h"

class SignallyApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "Signally"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed()          override { return false; }

    void initialise(const juce::String&) override
    {
        mainWindow_ = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow_.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(SignallyApplication)
