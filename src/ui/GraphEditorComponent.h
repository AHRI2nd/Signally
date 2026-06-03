#pragma once
#include <JuceHeader.h>
#include "NodeComponent.h"
#include "engine/AudioEngine.h"
#include <memory>
#include <vector>
#include <optional>

// Central canvas: displays nodes and connections, handles drag-to-connect.
class GraphEditorComponent : public juce::Component,
                             public juce::Timer
{
public:
    explicit GraphEditorComponent(AudioEngine& engine);
    ~GraphEditorComponent() override;

    // Add a node visually (after it has been added to the engine/graph)
    void addNodeComponent(MixingGraph::NodeID id,
                          const juce::String& name,
                          int numInputs, int numOutputs,
                          juce::Point<int> position = {});

    void removeNodeComponent(MixingGraph::NodeID id);

    // Called by DeviceManagerPanel / VST3BrowserPanel
    void addInputDeviceNode(const DeviceInfo& info);
    void addOutputDeviceNode(const DeviceInfo& info, IsolationMode mode);
    void addVirtualMicNode();
    void addMixerNode(int numInputs);
    void addSplitterNode(int numOutputs);
    void addVST3Node(const juce::PluginDescription& desc);

    AudioEngine& engine() { return engine_; }

    // Session persistence (Phase 7)
    void saveSession(const juce::File& file);
    void loadSession(const juce::File& file);
    void clearSession();

    // Called by NodeComponent during a pin drag to update the live preview line.
    void updatePendingDrag(juce::Point<float> worldPos);

protected:
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key) override;

public:
    // Zoom the canvas (Ctrl + / Ctrl - / Ctrl 0). Clamped to a sensible range.
    void setZoom(float newZoom);
    float getZoom() const { return zoom_; }

    // Resize the canvas so it always fills the viewport at the current zoom and
    // still contains every node (call after zoom / add / move).
    void updateCanvasBounds();

private:
    struct Connection
    {
        MixingGraph::NodeID srcNode, dstNode;
        int srcCh, dstCh;
    };

    // Captures everything needed to recreate a node on load.
    enum class NodeKind { InputDevice, OutputDevice, VirtualMic, Mixer, Splitter, VST3 };
    struct NodeDescriptor
    {
        MixingGraph::NodeID id;
        NodeKind            kind;
        juce::String        name;
        std::wstring        deviceId;        // InputDevice / OutputDevice
        IsolationMode       isolation = IsolationMode::Shared;
        int                 numChannels = 2;
        int                 busCount  = 2;   // Mixer inputs / Splitter outputs
        juce::String        pluginIdentifier; // VST3 uniqueId for re-scan
        juce::Point<int>    position;
        juce::String        pluginDescXml;    // VST3: full PluginDescription XML for reload
    };
    std::vector<NodeDescriptor> descriptors_;

    NodeDescriptor* findDescriptor(MixingGraph::NodeID id);
    juce::Point<int> nextNodePosition() const;

    void drawConnections(juce::Graphics& g);
    void drawPendingConnection(juce::Graphics& g);
    NodeComponent* findNodeAt(juce::Point<float> pos) const;
    int            pinHitTest(juce::Point<float> worldPos,
                               NodeComponent*& outNode,
                               bool& outIsInput) const;

    void onPinDragStart(NodeComponent* node, int pin, bool isInput, juce::Point<float> pos);
    void onPinDragEnd  (NodeComponent* node, int pin, bool isInput, juce::Point<float> pos);
    void onNodeRemove  (MixingGraph::NodeID id);

    // Right-click menu for a node: remove, plus per-bus gain for Mixer nodes.
    void showNodeContextMenu(MixingGraph::NodeID id);

    // Connection hit-testing / deletion (editor coordinate space).
    int  findConnectionAt(juce::Point<float> pos) const;
    void removeConnectionAt(int index);

    AudioEngine& engine_;

    std::vector<std::unique_ptr<NodeComponent>> nodes_;
    std::vector<Connection>                     connections_;

    // Pending connection drag state
    struct PendingConn
    {
        NodeComponent* srcNode  = nullptr;
        int            srcPin   = -1;
        bool           isInput  = false;
        juce::Point<float> startPos;
        juce::Point<float> currentPos;
    };
    std::optional<PendingConn> pending_;

    float zoom_ = 1.0f;
    juce::String dbg_;   // TEMP: connection diagnostics overlay

    juce::KnownPluginList pluginList_;
    juce::AudioPluginFormatManager formatManager_;
    bool formatsRegistered_ = false;
};
