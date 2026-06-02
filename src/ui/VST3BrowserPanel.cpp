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
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d3557));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(btn);
    };
    styleBtn(scanBtn_);
    styleBtn(addBtn_);

    scanBtn_.onClick = [this] { scanPlugins(); };
    addBtn_.onClick  = [this] { onAddPlugin(); };
}

VST3BrowserPanel::~VST3BrowserPanel() = default;

void VST3BrowserPanel::resized()
{
    auto area = getLocalBounds().reduced(6);
    titleLabel_.setBounds(area.removeFromTop(18));
    pluginList_ .setBounds(area.removeFromTop(260));
    scanBtn_    .setBounds(area.removeFromTop(26).reduced(0, 2));
    addBtn_     .setBounds(area.removeFromTop(26).reduced(0, 2));
    statusLabel_.setBounds(area.removeFromTop(20));
}

void VST3BrowserPanel::scanPlugins()
{
    statusLabel_.setText("Scanning...", juce::dontSendNotification);

    // Default VST3 search paths on Windows
    juce::FileSearchPath searchPath;
    searchPath.add(juce::File("C:\\Program Files\\Common Files\\VST3"));
    searchPath.add(juce::File("C:\\Program Files (x86)\\Common Files\\VST3"));
    auto userVST3 = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("VST3");
    if (userVST3.isDirectory()) searchPath.add(userVST3);

    juce::PluginDirectoryScanner scanner(knownList_, *formatManager_.getFormat(0),
                                          searchPath, true, {});
    juce::String nextPlugin;
    while (scanner.scanNextFile(true, nextPlugin))
        statusLabel_.setText("Scanning: " + nextPlugin, juce::dontSendNotification);

    pluginList_.updateContent();
    statusLabel_.setText(juce::String(knownList_.getNumTypes()) + " plugins found",
                          juce::dontSendNotification);
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
