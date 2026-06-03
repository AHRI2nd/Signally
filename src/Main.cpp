#include <JuceHeader.h>
#include "MainWindow.h"
#include "ui/SignallyLookAndFeel.h"

class SignallyApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "Signally"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed()          override { return false; }

    void initialise(const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);
        mainWindow_ = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    SignallyLookAndFeel         lookAndFeel_;   // installed before the window, destroyed after
    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(SignallyApplication)
