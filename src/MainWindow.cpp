#include "MainWindow.h"

static const juce::Colour kBgColour{ 0xff1c1f26 };
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

    startBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2ea043));
    startBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    stopBtn_ .setColour(juce::TextButton::buttonColourId, juce::Colour(0xffda3633));
    stopBtn_ .setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    stopBtn_.setEnabled(false);

    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel_.setFont(juce::Font(12.0f));

    auto styleToolBtn = [this](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff343b47));
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(b);
    };
    styleToolBtn(saveBtn_);
    styleToolBtn(loadBtn_);

    // ── Format selectors (sample rate + virtual-mic bit depth) ────────────────
    auto styleCombo = [this](juce::ComboBox& c) {
        c.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff282d36));
        c.setColour(juce::ComboBox::textColourId,       juce::Colours::white);
        c.setColour(juce::ComboBox::arrowColourId,      juce::Colours::lightgrey);
        addAndMakeVisible(c);
    };

    sampleRateBox_.addItem("44.1 kHz", 1);
    sampleRateBox_.addItem("48 kHz",   2);
    sampleRateBox_.addItem("96 kHz",   3);
    sampleRateBox_.setSelectedId(2, juce::dontSendNotification); // 48 kHz default
    sampleRateBox_.onChange = [this] {
        int sr = 48000;
        switch (sampleRateBox_.getSelectedId()) { case 1: sr = 44100; break; case 3: sr = 96000; break; default: sr = 48000; }
        engine_.setSampleRate((double) sr);
    };
    styleCombo(sampleRateBox_);

    bitDepthBox_.addItem("16-bit",        16);
    bitDepthBox_.addItem("24-bit",        24);
    bitDepthBox_.addItem("32-bit float",  32);
    bitDepthBox_.setSelectedId(32, juce::dontSendNotification); // float passthrough default
    bitDepthBox_.onChange = [this] {
        engine_.setMicBitDepth(bitDepthBox_.getSelectedId());
    };
    styleCombo(bitDepthBox_);

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
            sampleRateBox_.setEnabled(false); // format is locked while running
            bitDepthBox_  .setEnabled(false);
            statusLabel_.setText("Engine running", juce::dontSendNotification);
        }
    };
    stopBtn_.onClick = [this]
    {
        stopEngine();
        startBtn_.setEnabled(true);
        stopBtn_ .setEnabled(false);
        sampleRateBox_.setEnabled(true);
        bitDepthBox_  .setEnabled(true);
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
    double cpu  = engine_.getCpuLoad() * 100.0;
    int    xrun = engine_.getUnderrunCount();
    int    act  = engine_.getActiveDeviceCount();
    int    tot  = engine_.getTotalDeviceCount();
    statusLabel_.setText("Running  |  Dev " + juce::String(act) + "/" + juce::String(tot)
                         + "  |  CPU " + juce::String(cpu, 1) + "%"
                         + "  |  Underruns " + juce::String(xrun),
                         juce::dontSendNotification);
}

void MainWindow::Content::paint(juce::Graphics& g)
{
    g.fillAll(kBgColour);

    // Toolbar separator
    g.setColour(juce::Colour(0xff14161b));
    g.fillRect(0, 0, getWidth(), kToolbarHeight);

    // Panel dividers
    g.setColour(juce::Colour(0xff2a2f3a));
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
    sampleRateBox_.setBounds(toolbar.removeFromLeft(90));
    toolbar.removeFromLeft(4);
    bitDepthBox_  .setBounds(toolbar.removeFromLeft(100));
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
