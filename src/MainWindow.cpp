#include "MainWindow.h"

static constexpr juce::Colour kBgColour{ 0xff0d1b2a };
static constexpr int kLeftPanelWidth  = 220;
static constexpr int kRightPanelWidth = 220;
static constexpr int kToolbarHeight   = 36;

// ── Content ──────────────────────────────────────────────────────────────────

MainWindow::Content::Content()
    : graphEditor_(engine_),
      devicePanel_(engine_, graphEditor_),
      vst3Panel_(graphEditor_)
{
    addAndMakeVisible(graphViewport_);
    graphViewport_.setViewedComponent(&graphEditor_, false);
    graphViewport_.setScrollBarsShown(true, true);

    addAndMakeVisible(devicePanel_);
    addAndMakeVisible(vst3Panel_);

    startBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2e7d32));
    startBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    stopBtn_ .setColour(juce::TextButton::buttonColourId, juce::Colour(0xffc62828));
    stopBtn_ .setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    stopBtn_.setEnabled(false);

    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel_.setFont(juce::Font(12.0f));

    auto styleToolBtn = [this](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d3557));
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(b);
    };
    styleToolBtn(saveBtn_);
    styleToolBtn(loadBtn_);

    addAndMakeVisible(startBtn_);
    addAndMakeVisible(stopBtn_);
    addAndMakeVisible(statusLabel_);

    saveBtn_.onClick = [this]
    {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Save Session", juce::File{}, "*.signally");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f != juce::File{})
                    graphEditor_.saveSession(f.withFileExtension("signally"));
            });
    };
    loadBtn_.onClick = [this]
    {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Load Session", juce::File{}, "*.signally");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f.existsAsFile())
                    graphEditor_.loadSession(f);
            });
    };

    startBtn_.onClick = [this]
    {
        if (startEngine())
        {
            startBtn_.setEnabled(false);
            stopBtn_ .setEnabled(true);
            statusLabel_.setText("Engine running", juce::dontSendNotification);
        }
    };
    stopBtn_.onClick = [this]
    {
        stopEngine();
        startBtn_.setEnabled(true);
        stopBtn_ .setEnabled(false);
        statusLabel_.setText("Engine stopped", juce::dontSendNotification);
    };

    engine_.onError = [this](const juce::String& msg)
    {
        juce::MessageManager::callAsync([this, msg] {
            statusLabel_.setText("Error: " + msg, juce::dontSendNotification);
        });
    };

    setSize(1400, 900);
}

MainWindow::Content::~Content()
{
    stopEngine();
}

bool MainWindow::Content::startEngine()
{
    if (!engine_.start())
    {
        statusLabel_.setText("Engine start failed", juce::dontSendNotification);
        return false;
    }
    engineRunning_ = true;
    startTimerHz(4); // update CPU/underrun readout 4x per second
    return true;
}

void MainWindow::Content::stopEngine()
{
    stopTimer();
    engine_.stop();
    engineRunning_ = false;
}

void MainWindow::Content::timerCallback()
{
    if (!engineRunning_) return;
    int    cpu = juce::roundToInt(engine_.getCpuLoad() * 100.0);
    int    xrun = engine_.getUnderrunCount();
    statusLabel_.setText("Running  |  CPU " + juce::String(cpu) + "%"
                         + "  |  Underruns " + juce::String(xrun),
                         juce::dontSendNotification);
}

void MainWindow::Content::paint(juce::Graphics& g)
{
    g.fillAll(kBgColour);

    // Toolbar separator
    g.setColour(juce::Colour(0xff1a2d42));
    g.fillRect(0, 0, getWidth(), kToolbarHeight);

    // Panel dividers
    g.setColour(juce::Colour(0xff1a3557));
    g.drawVerticalLine(kLeftPanelWidth, kToolbarHeight, (float)getHeight());
    g.drawVerticalLine(getWidth() - kRightPanelWidth, kToolbarHeight, (float)getHeight());
}

void MainWindow::Content::resized()
{
    // Toolbar
    auto toolbar = getLocalBounds().removeFromTop(kToolbarHeight).reduced(4, 4);
    startBtn_   .setBounds(toolbar.removeFromLeft(110));
    toolbar.removeFromLeft(4);
    stopBtn_    .setBounds(toolbar.removeFromLeft(110));
    toolbar.removeFromLeft(8);
    saveBtn_    .setBounds(toolbar.removeFromLeft(70));
    toolbar.removeFromLeft(4);
    loadBtn_    .setBounds(toolbar.removeFromLeft(70));
    toolbar.removeFromLeft(8);
    statusLabel_.setBounds(toolbar);

    auto remaining = getLocalBounds().withTrimmedTop(kToolbarHeight);

    // Left panel — device manager
    devicePanel_.setBounds(remaining.removeFromLeft(kLeftPanelWidth));

    // Right panel — VST3 browser
    vst3Panel_.setBounds(remaining.removeFromRight(kRightPanelWidth));

    // Centre — graph viewport
    graphViewport_.setBounds(remaining);
}

// ── MainWindow ───────────────────────────────────────────────────────────────

MainWindow::MainWindow()
    : juce::DocumentWindow("Signally",
                            juce::Colour(0xff0d1b2a),
                            juce::DocumentWindow::allButtons)
{
    content_ = std::make_unique<Content>();
    setContentNonOwned(content_.get(), true);
    setResizable(true, false);
    setUsingNativeTitleBar(true);
    centreWithSize(1400, 900);
    setVisible(true);
}

MainWindow::~MainWindow()
{
    content_.reset();
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
