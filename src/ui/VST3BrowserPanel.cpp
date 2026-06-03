#include "VST3BrowserPanel.h"
#include "GraphEditorComponent.h"

VST3BrowserPanel::VST3BrowserPanel(GraphEditorComponent& editor)
    : editor_(editor)
{
    formatManager_.addDefaultFormats();
    listModel_.list = &knownList_;

    titleLabel_.setFont(juce::Font(13.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    pluginList_.setModel(&listModel_);
    addAndMakeVisible(pluginList_);

    statusLabel_.setFont(juce::Font(11.0f));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel_);

    auto styleBtn = [this](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff343b47));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(btn);
    };
    styleBtn(scanBtn_);
    styleBtn(addBtn_);

    scanBtn_.onClick = [this] { scanPluginsAsync(); };
    addBtn_.onClick  = [this] { onAddPlugin(); };
    // Scanning is started manually via the "Scan VST3 Folders" button.
}

VST3BrowserPanel::~VST3BrowserPanel()
{
    if (scanner_) scanner_->stopThread(3000);
}

void VST3BrowserPanel::resized()
{
    auto area = getLocalBounds().reduced(6);
    titleLabel_.setBounds(area.removeFromTop(18));
    pluginList_ .setBounds(area.removeFromTop(260));
    scanBtn_    .setBounds(area.removeFromTop(26).reduced(0, 2));
    addBtn_     .setBounds(area.removeFromTop(26).reduced(0, 2));
    statusLabel_.setBounds(area.removeFromTop(20));
}

void VST3BrowserPanel::scanPluginsAsync()
{
    if (scanning_.exchange(true)) return;  // a scan is already running
    statusLabel_.setText("Scanning VST3...", juce::dontSendNotification);
    scanner_ = std::make_unique<Scanner>(*this);
    scanner_->startThread();
}

void VST3BrowserPanel::doScan(juce::Thread& thread)
{
    // System VST3 search paths on Windows.
    juce::FileSearchPath searchPath;
    searchPath.add(juce::File("C:\\Program Files\\Common Files\\VST3"));
    searchPath.add(juce::File("C:\\Program Files (x86)\\Common Files\\VST3"));
    auto userVST3 = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("VST3");
    if (userVST3.isDirectory()) searchPath.add(userVST3);

    auto* fmt = formatManager_.getFormat(0);
    if (fmt == nullptr) { scanning_ = false; return; }

    juce::PluginDirectoryScanner scanner(knownList_, *fmt, searchPath, true, {});
    juce::Component::SafePointer<VST3BrowserPanel> safe(this);

    juce::String nextPlugin;
    while (!thread.threadShouldExit() && scanner.scanNextFile(true, nextPlugin))
    {
        juce::MessageManager::callAsync([safe, nextPlugin] {
            if (safe) safe->statusLabel_.setText("Scanning: " + nextPlugin, juce::dontSendNotification);
        });
    }

    juce::MessageManager::callAsync([safe] {
        if (safe == nullptr) return;
        safe->pluginList_.updateContent();
        safe->statusLabel_.setText(juce::String(safe->knownList_.getNumTypes()) + " plugins found",
                                   juce::dontSendNotification);
        safe->scanning_ = false;
    });
}

void VST3BrowserPanel::onAddPlugin()
{
    int row = pluginList_.getSelectedRow();
    if (row < 0 || row >= knownList_.getNumTypes()) return;
    editor_.addVST3Node(knownList_.getTypes()[row]);
}

void VST3BrowserPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    pluginList_.updateContent();
}
