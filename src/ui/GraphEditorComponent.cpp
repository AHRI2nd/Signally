#include "GraphEditorComponent.h"
#include "ConnectionComponent.h"
#include "nodes/MixerNode.h"
#include "nodes/SplitterNode.h"
#include "nodes/VST3PluginNode.h"
#include "nodes/VirtualMicOutputNode.h"
#include "nodes/InputDeviceNode.h"
#include "nodes/OutputDeviceNode.h"

static const juce::Colour kCanvasBg   { 0xff16181d };  // neutral slate
static const juce::Colour kGridColour { 0xff242832 };
static const juce::Colour kConnColour { 0xff6ea8fe };  // soft blue
static const juce::Colour kPendingColour { 0xfff2c14e };  // warm amber
static constexpr int kGridSize = 24;

GraphEditorComponent::GraphEditorComponent(AudioEngine& engine)
    : engine_(engine)
{
    setSize(2000, 1400);
    setWantsKeyboardFocus(true);     // receive Ctrl +/- zoom keys
    startTimer(60); // repaint at ~60 Hz for connection preview
}

GraphEditorComponent::~GraphEditorComponent()
{
    stopTimer();
}

void GraphEditorComponent::addNodeComponent(MixingGraph::NodeID id,
                                             const juce::String& name,
                                             int numInputs, int numOutputs,
                                             juce::Point<int> position)
{
    auto* nc = nodes_.emplace_back(
        std::make_unique<NodeComponent>(*this, id, name, numInputs, numOutputs)).get();

    nc->onPinDragStart = [this](auto* n, int pin, bool isIn, auto pos) {
        onPinDragStart(n, pin, isIn, pos);
    };
    nc->onPinDragEnd   = [this](auto* n, int pin, bool isIn, auto pos) {
        onPinDragEnd(n, pin, isIn, pos);
    };
    nc->onContextMenu = [this](auto nid) { showNodeContextMenu(nid); };

    if (position.isOrigin())
        position = { 80 + (int)nodes_.size() * 30, 80 + (int)nodes_.size() * 30 };

    nc->setTopLeftPosition(position);
    addAndMakeVisible(nc);
    updateCanvasBounds();
    repaint();
}

void GraphEditorComponent::removeNodeComponent(MixingGraph::NodeID id)
{
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
                            [id](auto& n) { return n->nodeId() == id; });
    if (it != nodes_.end())
    {
        removeChildComponent(it->get());
        nodes_.erase(it);
    }
    connections_.erase(std::remove_if(connections_.begin(), connections_.end(),
        [id](auto& c) { return c.srcNode == id || c.dstNode == id; }),
        connections_.end());
    descriptors_.erase(std::remove_if(descriptors_.begin(), descriptors_.end(),
        [id](auto& d) { return d.id == id; }),
        descriptors_.end());
    repaint();
}

void GraphEditorComponent::addInputDeviceNode(const DeviceInfo& info)
{
    // ASIO devices must use the ASIO backend; otherwise default to Exclusive capture.
    IsolationMode mode = info.isAsio ? IsolationMode::ASIO : IsolationMode::Exclusive;
    auto id = engine_.addInputDevice(info.id, mode, 48000.0, 480, info.maxChannels);
    auto pos = nextNodePosition();
    descriptors_.push_back({ id, NodeKind::InputDevice, info.name, info.id,
                             mode, info.maxChannels, 2, {}, pos });
    addNodeComponent(id, info.name, 0, info.maxChannels, pos);
}

void GraphEditorComponent::addOutputDeviceNode(const DeviceInfo& info, IsolationMode mode)
{
    // An ASIO device always uses the ASIO backend regardless of the requested mode.
    IsolationMode actualMode = info.isAsio ? IsolationMode::ASIO : mode;
    auto id = engine_.addOutputDevice(info.id, actualMode, 48000.0, 480, info.maxChannels);
    auto pos = nextNodePosition();
    descriptors_.push_back({ id, NodeKind::OutputDevice, info.name, info.id,
                             actualMode, info.maxChannels, 2, {}, pos });
    addNodeComponent(id, info.name, info.maxChannels, 0, pos);
}

void GraphEditorComponent::addVirtualMicNode()
{
    auto id = engine_.addVirtualMicOutput(2);
    auto pos = nextNodePosition();
    descriptors_.push_back({ id, NodeKind::VirtualMic, "Virtual Mic", {},
                             IsolationMode::Shared, 2, 2, {}, pos });
    addNodeComponent(id, "Virtual Mic", 2, 0, pos);
}

void GraphEditorComponent::addMixerNode(int numInputs)
{
    auto node = std::make_unique<MixerNode>(numInputs, 2);
    auto id   = engine_.graph().addNode(std::move(node));
    auto pos  = nextNodePosition();
    descriptors_.push_back({ id, NodeKind::Mixer, "Mixer", {},
                             IsolationMode::Shared, 2, numInputs, {}, pos });
    addNodeComponent(id, "Mixer", numInputs * 2, 2, pos);
}

void GraphEditorComponent::addSplitterNode(int numOutputs)
{
    auto node = std::make_unique<SplitterNode>(numOutputs, 2);
    auto id   = engine_.graph().addNode(std::move(node));
    auto pos  = nextNodePosition();
    descriptors_.push_back({ id, NodeKind::Splitter, "Splitter", {},
                             IsolationMode::Shared, 2, numOutputs, {}, pos });
    addNodeComponent(id, "Splitter", 2, numOutputs * 2, pos);
}

void GraphEditorComponent::addVST3Node(const juce::PluginDescription& desc)
{
    if (!formatsRegistered_)
    {
        formatManager_.addDefaultFormats();
        formatsRegistered_ = true;
    }

    juce::String errMsg;
    auto instance = formatManager_.createPluginInstance(
        desc, 48000.0, 480, errMsg);
    if (!instance)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Plugin load failed", errMsg);
        return;
    }

    int ins  = instance->getTotalNumInputChannels();
    int outs = instance->getTotalNumOutputChannels();
    juce::String pname = instance->getName();

    auto node = std::make_unique<VST3PluginNode>(std::move(instance));
    auto id   = engine_.graph().addNode(std::move(node));
    auto pos  = nextNodePosition();

    NodeDescriptor d{ id, NodeKind::VST3, pname, {},
                      IsolationMode::Shared, 2, 2, desc.createIdentifierString(), pos };
    if (auto descXml = desc.createXml())
        d.pluginDescXml = descXml->toString();
    descriptors_.push_back(d);
    addNodeComponent(id, pname, ins, outs, pos);
}

GraphEditorComponent::NodeDescriptor* GraphEditorComponent::findDescriptor(MixingGraph::NodeID id)
{
    for (auto& d : descriptors_)
        if (d.id == id) return &d;
    return nullptr;
}

juce::Point<int> GraphEditorComponent::nextNodePosition() const
{
    // Centre new nodes in the currently-visible viewport area, staggering each
    // one slightly so successive additions don't perfectly overlap.
    int n = (int) nodes_.size();
    juce::Point<int> centre(getWidth() / 2, getHeight() / 2);
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        centre = vp->getViewArea().getCentre();

    const int stagger = (n % 6) * 26;
    return centre + juce::Point<int>(stagger - 65 - NodeComponent::kMinWidth / 2,
                                     stagger - 65);
}

void GraphEditorComponent::paint(juce::Graphics& g)
{
    // Canvas background — vertical gradient
    g.setGradientFill(juce::ColourGradient(
        kCanvasBg.brighter(0.05f), 0.0f, 0.0f,
        kCanvasBg.darker(0.12f),   0.0f, (float) getHeight(), false));
    g.fillRect(getLocalBounds());

    // Grid — minor lines, with brighter major lines every 4 cells
    for (int x = 0; x < getWidth(); x += kGridSize)
    {
        g.setColour((x % (kGridSize * 4) == 0) ? kGridColour.brighter(0.45f) : kGridColour);
        g.drawVerticalLine(x, 0.0f, (float) getHeight());
    }
    for (int y = 0; y < getHeight(); y += kGridSize)
    {
        g.setColour((y % (kGridSize * 4) == 0) ? kGridColour.brighter(0.45f) : kGridColour);
        g.drawHorizontalLine(y, 0.0f, (float) getWidth());
    }

    drawConnections(g);
    drawPendingConnection(g);

    // Empty-state hint
    if (nodes_.empty())
    {
        auto area = getLocalBounds();
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
            area = vp->getViewArea();
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.setFont(juce::Font(17.0f));
        g.drawFittedText("Add a device from the left panel,\n"
                         "then drag pin to pin to connect.\n\n"
                         "Ctrl + scroll to zoom",
                         area, juce::Justification::centred, 4);
    }
}

void GraphEditorComponent::resized() {}

void GraphEditorComponent::setZoom(float newZoom)
{
    zoom_ = juce::jlimit(0.35f, 3.0f, newZoom);
    // Scale around the top-left; the enclosing Viewport handles panning.
    setTransform(juce::AffineTransform::scale(zoom_));
    updateCanvasBounds();
}

void GraphEditorComponent::updateCanvasBounds()
{
    // Bounding box of all node components (logical coords).
    juce::Rectangle<int> content;
    for (auto& n : nodes_)
        content = content.getUnion(n->getBounds());

    // Logical size needed to fill the visible viewport at the current zoom.
    int vw = 1200, vh = 800;
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        vw = vp->getViewWidth();
        vh = vp->getViewHeight();
    }
    const int visW = (int) std::ceil(vw / zoom_);
    const int visH = (int) std::ceil(vh / zoom_);

    // Zoom OUT (zoom_<1) → visW/visH grow → canvas expands.
    // Zoom IN  (zoom_>1) → visW/visH shrink → canvas follows node content if any
    //   node lies beyond the viewport (scrollable), else shrinks to fit.
    const int w = juce::jmax(visW, content.getRight()  + 300);
    const int h = juce::jmax(visH, content.getBottom() + 300);

    if (w != getWidth() || h != getHeight())
        setSize(w, h);
}

void GraphEditorComponent::mouseWheelMove(const juce::MouseEvent& e,
                                          const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())  // Ctrl + wheel = zoom
    {
        setZoom(zoom_ * (1.0f + wheel.deltaY * 0.6f));
        return;
    }
    // Otherwise let the enclosing Viewport scroll as usual.
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        vp->mouseWheelMove(e.getEventRelativeTo(vp), wheel);
}

bool GraphEditorComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown())  // Ctrl on Windows
    {
        const auto code = key.getKeyCode();
        const auto ch   = key.getTextCharacter();
        if (ch == '+' || ch == '=' || code == juce::KeyPress::numberPadAdd)
        {
            setZoom(zoom_ * 1.1f);
            return true;
        }
        if (ch == '-' || ch == '_' || code == juce::KeyPress::numberPadSubtract)
        {
            setZoom(zoom_ / 1.1f);
            return true;
        }
        if (ch == '0')
        {
            setZoom(1.0f);
            return true;
        }
    }
    return false;
}

void GraphEditorComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();  // ensure Ctrl +/- zoom keys reach this component

    if (e.mods.isRightButtonDown())
    {
        // Right-clicking a connection line offers to delete it.
        int connIdx = findConnectionAt(e.position);
        if (connIdx >= 0)
        {
            juce::PopupMenu connMenu;
            connMenu.addItem(1, "Delete Connection");
            connMenu.showMenuAsync(juce::PopupMenu::Options{}, [this, connIdx](int result) {
                if (result == 1) removeConnectionAt(connIdx);
            });
            return;
        }

        // Otherwise, context menu to add nodes.
        juce::PopupMenu menu;
        menu.addItem(1, "Add Input Device");
        menu.addItem(2, "Add Output Device (Shared)");
        menu.addItem(3, "Add Output Device (Exclusive)");
        menu.addItem(4, "Add Virtual Mic Output");
        menu.addItem(5, "Add Mixer (2 inputs)");
        menu.addItem(6, "Add Mixer (4 inputs)");
        menu.addItem(7, "Add Splitter (2 outputs)");
        menu.addItem(8, "Load VST3 Plugin...");

        menu.showMenuAsync(juce::PopupMenu::Options{}, [this](int result) {
            switch (result)
            {
                case 4: addVirtualMicNode(); break;
                case 5: addMixerNode(2);    break;
                case 6: addMixerNode(4);    break;
                case 7: addSplitterNode(2); break;
                case 8:
                {
                    auto chooser = std::make_shared<juce::FileChooser>(
                        "Select VST3 Plugin", juce::File{}, "*.vst3");
                    chooser->launchAsync(juce::FileBrowserComponent::openMode, [this, chooser](const juce::FileChooser&) {
                        auto file = chooser->getResult();
                        if (!file.existsAsFile()) return;

                        if (!formatsRegistered_)
                        {
                            formatManager_.addDefaultFormats();
                            formatsRegistered_ = true;
                        }

                        juce::OwnedArray<juce::PluginDescription> descs;
                        for (auto* fmt : formatManager_.getFormats())
                            fmt->findAllTypesForFile(descs, file.getFullPathName());

                        if (!descs.isEmpty())
                            addVST3Node(*descs[0]);
                    });
                    break;
                }
                default: break;
            }
        });
    }
}

void GraphEditorComponent::timerCallback()
{
    if (pending_.has_value()) repaint();
}

static juce::Path makeBezier(juce::Point<float> start, juce::Point<float> end)
{
    juce::Path p;
    p.startNewSubPath(start);
    float dx = (end.x - start.x) * 0.5f;
    p.cubicTo(start.x + dx, start.y,
               end.x  - dx, end.y,
               end.x, end.y);
    return p;
}

void GraphEditorComponent::drawConnections(juce::Graphics& g)
{
    g.setColour(kConnColour);
    for (auto& c : connections_)
    {
        NodeComponent* srcNode = nullptr;
        NodeComponent* dstNode = nullptr;
        for (auto& n : nodes_)
        {
            if (n->nodeId() == c.srcNode) srcNode = n.get();
            if (n->nodeId() == c.dstNode) dstNode = n.get();
        }
        if (!srcNode || !dstNode) continue;

        auto start = srcNode->getPinPosition(c.srcCh, false);
        auto end   = dstNode->getPinPosition(c.dstCh, true);
        g.strokePath(makeBezier(start, end), juce::PathStrokeType(2.0f));
    }
}

void GraphEditorComponent::drawPendingConnection(juce::Graphics& g)
{
    if (!pending_.has_value()) return;
    g.setColour(kPendingColour);
    g.strokePath(makeBezier(pending_->startPos, pending_->currentPos),
                 juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                       juce::PathStrokeType::rounded));
}

void GraphEditorComponent::onPinDragStart(NodeComponent* node, int pin,
                                           bool isInput, juce::Point<float> pos)
{
    pending_ = PendingConn{ node, pin, isInput, pos, pos };
}

void GraphEditorComponent::updatePendingDrag(juce::Point<float> worldPos)
{
    if (pending_.has_value())
    {
        pending_->currentPos = worldPos;
        repaint();
    }
}

void GraphEditorComponent::onPinDragEnd(NodeComponent* node, int pin,
                                         bool isInput, juce::Point<float> worldPos)
{
    if (!pending_.has_value()) return;

    // Target pins are the opposite type of the dragged pin (output→input).
    const bool  targetIsInput = !pending_->isInput;

    // Snap to the NEAREST compatible pin within a forgiving radius, rather than
    // requiring the drop to land exactly on the small pin circle.
    NodeComponent* bestNode = nullptr;
    int            bestPin  = -1;
    float          bestDist = 1.0e9f;

    for (auto& n : nodes_)
    {
        if (n.get() == node) continue;  // no self-connection
        const int count = targetIsInput ? n->numInputs() : n->numOutputs();
        for (int i = 0; i < count; ++i)
        {
            float d = worldPos.getDistanceFrom(n->getPinPosition(i, targetIsInput));
            if (d < bestDist) { bestDist = d; bestNode = n.get(); bestPin = i; }
        }
    }

    constexpr float kSnapRadius = 30.0f;
    if (bestNode != nullptr && bestDist <= kSnapRadius)
    {
        NodeComponent* srcNode = pending_->isInput ? bestNode : node;
        int            srcPin  = pending_->isInput ? bestPin  : pin;
        NodeComponent* dstNode = pending_->isInput ? node     : bestNode;
        int            dstPin  = pending_->isInput ? pin      : bestPin;

        if (engine_.connect(srcNode->nodeId(), srcPin, dstNode->nodeId(), dstPin))
            connections_.push_back({ srcNode->nodeId(), dstNode->nodeId(), srcPin, dstPin });
    }

    pending_.reset();
    repaint();
}

void GraphEditorComponent::onNodeRemove(MixingGraph::NodeID id)
{
    engine_.removeNode(id);
    removeNodeComponent(id);
}

void GraphEditorComponent::showNodeContextMenu(MixingGraph::NodeID id)
{
    auto* desc = findDescriptor(id);

    juce::PopupMenu menu;

    // For a Mixer node, offer a per-input-bus gain submenu.
    MixerNode* mixer = nullptr;
    if (desc && desc->kind == NodeKind::Mixer)
        if (auto* gn = engine_.graph().graph().getNodeForId(id))
            mixer = dynamic_cast<MixerNode*>(gn->getProcessor());

    if (mixer != nullptr)
    {
        static const std::pair<const char*, float> kLevels[] = {
            { "Mute",   0.0f   }, { "-12 dB", 0.251f }, { "-6 dB",  0.501f },
            { "0 dB",   1.0f   }, { "+6 dB",  1.995f }, { "+12 dB", 3.981f },
        };

        for (int bus = 0; bus < desc->busCount; ++bus)
        {
            const float current = mixer->getBusGain(bus);
            juce::PopupMenu busMenu;
            for (const auto& lv : kLevels)
            {
                const bool ticked = std::abs(current - lv.second) < 0.01f;
                busMenu.addItem(lv.first, true, ticked,
                                [mixer, bus, gain = lv.second] { mixer->setBusGain(bus, gain); });
            }
            menu.addSubMenu("Input " + juce::String(bus) + " gain", busMenu);
        }
        menu.addSeparator();
    }

    menu.addItem("Remove Node", [this, id] { onNodeRemove(id); });
    menu.showMenuAsync(juce::PopupMenu::Options{});
}

int GraphEditorComponent::findConnectionAt(juce::Point<float> pos) const
{
    // Iterate front-to-back so the topmost (most recently added) line wins.
    for (int i = (int)connections_.size() - 1; i >= 0; --i)
    {
        const auto& c = connections_[i];
        const NodeComponent* srcNode = nullptr;
        const NodeComponent* dstNode = nullptr;
        for (auto& n : nodes_)
        {
            if (n->nodeId() == c.srcNode) srcNode = n.get();
            if (n->nodeId() == c.dstNode) dstNode = n.get();
        }
        if (!srcNode || !dstNode) continue;

        auto start = srcNode->getPinPosition(c.srcCh, false);
        auto end   = dstNode->getPinPosition(c.dstCh, true);
        if (ConnectionComponent::hitTest(ConnectionComponent::makeBezier(start, end), pos))
            return i;
    }
    return -1;
}

void GraphEditorComponent::removeConnectionAt(int index)
{
    if (index < 0 || index >= (int)connections_.size()) return;

    const auto c = connections_[index];
    engine_.disconnect(c.srcNode, c.srcCh, c.dstNode, c.dstCh);
    connections_.erase(connections_.begin() + index);
    repaint();
}

// ── Session persistence ───────────────────────────────────────────────────────

void GraphEditorComponent::saveSession(const juce::File& file)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();

    // Nodes
    juce::Array<juce::var> nodesArr;
    for (auto& d : descriptors_)
    {
        // Sync live position from the NodeComponent
        juce::Point<int> pos = d.position;
        for (auto& n : nodes_)
            if (n->nodeId() == d.id) pos = n->getPosition();

        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("id",        (int)d.id.uid);
        o->setProperty("kind",      (int)d.kind);
        o->setProperty("name",      d.name);
        o->setProperty("deviceId",  juce::String(d.deviceId.c_str()));
        o->setProperty("isolation", (int)d.isolation);
        o->setProperty("channels",  d.numChannels);
        o->setProperty("busCount",  d.busCount);
        o->setProperty("plugin",    d.pluginIdentifier);
        o->setProperty("x",         pos.x);
        o->setProperty("y",         pos.y);

        // VST3: persist the full plugin description plus its live state so the
        // exact plugin can be recreated and restored on load.
        if (d.kind == NodeKind::VST3)
        {
            o->setProperty("pluginDesc", d.pluginDescXml);
            if (auto* gn = engine_.graph().graph().getNodeForId(d.id))
            {
                juce::MemoryBlock mb;
                gn->getProcessor()->getStateInformation(mb);
                o->setProperty("pluginState", mb.toBase64Encoding());
            }
        }

        nodesArr.add(juce::var(o.get()));
    }
    root->setProperty("nodes", nodesArr);

    // Connections
    juce::Array<juce::var> connArr;
    for (auto& c : connections_)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("srcNode", (int)c.srcNode.uid);
        o->setProperty("srcCh",   c.srcCh);
        o->setProperty("dstNode", (int)c.dstNode.uid);
        o->setProperty("dstCh",   c.dstCh);
        connArr.add(juce::var(o.get()));
    }
    root->setProperty("connections", connArr);

    file.replaceWithText(juce::JSON::toString(juce::var(root.get()), true));
}

void GraphEditorComponent::loadSession(const juce::File& file)
{
    auto parsed = juce::JSON::parse(file);
    if (!parsed.isObject()) return;

    clearSession();

    // Map saved IDs → freshly created NodeIDs.
    std::unordered_map<int, MixingGraph::NodeID> idMap;

    auto nodesArr = parsed.getProperty("nodes", {});
    if (auto* arr = nodesArr.getArray())
    {
        for (auto& v : *arr)
        {
            int  savedId = (int)v.getProperty("id", 0);
            auto kind    = (NodeKind)(int)v.getProperty("kind", 0);
            auto name    = v.getProperty("name", "").toString();
            auto devId   = v.getProperty("deviceId", "").toString();
            auto iso     = (IsolationMode)(int)v.getProperty("isolation", 0);
            int  ch      = (int)v.getProperty("channels", 2);
            int  buses   = (int)v.getProperty("busCount", 2);
            int  x       = (int)v.getProperty("x", 80);
            int  y       = (int)v.getProperty("y", 80);

            MixingGraph::NodeID newId;
            switch (kind)
            {
                case NodeKind::InputDevice:
                {
                    newId = engine_.addInputDevice(devId.toWideCharPointer(), iso, 48000.0, 480, ch);
                    descriptors_.push_back({ newId, kind, name, devId.toWideCharPointer(), iso, ch, 2, {}, {x,y} });
                    addNodeComponent(newId, name, 0, ch, {x,y});
                    break;
                }
                case NodeKind::OutputDevice:
                {
                    newId = engine_.addOutputDevice(devId.toWideCharPointer(), iso, 48000.0, 480, ch);
                    descriptors_.push_back({ newId, kind, name, devId.toWideCharPointer(), iso, ch, 2, {}, {x,y} });
                    addNodeComponent(newId, name, ch, 0, {x,y});
                    break;
                }
                case NodeKind::VirtualMic:
                {
                    newId = engine_.addVirtualMicOutput(ch);
                    descriptors_.push_back({ newId, kind, name, {}, iso, ch, 2, {}, {x,y} });
                    addNodeComponent(newId, name, ch, 0, {x,y});
                    break;
                }
                case NodeKind::Mixer:
                {
                    newId = engine_.graph().addNode(std::make_unique<MixerNode>(buses, 2));
                    descriptors_.push_back({ newId, kind, name, {}, iso, ch, buses, {}, {x,y} });
                    addNodeComponent(newId, name, buses * 2, 2, {x,y});
                    break;
                }
                case NodeKind::Splitter:
                {
                    newId = engine_.graph().addNode(std::make_unique<SplitterNode>(buses, 2));
                    descriptors_.push_back({ newId, kind, name, {}, iso, ch, buses, {}, {x,y} });
                    addNodeComponent(newId, name, 2, buses * 2, {x,y});
                    break;
                }
                case NodeKind::VST3:
                {
                    auto descXml = v.getProperty("pluginDesc", "").toString();
                    if (descXml.isEmpty()) continue; // legacy session without plugin info

                    juce::PluginDescription pd;
                    if (auto xml = juce::parseXML(descXml))
                        pd.loadFromXml(*xml);

                    if (!formatsRegistered_)
                    {
                        formatManager_.addDefaultFormats();
                        formatsRegistered_ = true;
                    }

                    juce::String errMsg;
                    auto instance = formatManager_.createPluginInstance(pd, 48000.0, 480, errMsg);
                    if (!instance) continue; // plugin no longer available on this machine

                    // Restore the plugin's saved state.
                    auto stateB64 = v.getProperty("pluginState", "").toString();
                    if (stateB64.isNotEmpty())
                    {
                        juce::MemoryBlock mb;
                        mb.fromBase64Encoding(stateB64);
                        instance->setStateInformation(mb.getData(), (int)mb.getSize());
                    }

                    int ins  = instance->getTotalNumInputChannels();
                    int outs = instance->getTotalNumOutputChannels();

                    newId = engine_.graph().addNode(std::make_unique<VST3PluginNode>(std::move(instance)));
                    descriptors_.push_back({ newId, kind, name, {}, iso, ch, 2,
                                             pd.createIdentifierString(), {x,y} });
                    descriptors_.back().pluginDescXml = descXml;
                    addNodeComponent(newId, name, ins, outs, {x,y});
                    break;
                }
            }
            idMap[savedId] = newId;
        }
    }

    // Restore connections using the ID map.
    auto connArr = parsed.getProperty("connections", {});
    if (auto* arr = connArr.getArray())
    {
        for (auto& v : *arr)
        {
            int sId = (int)v.getProperty("srcNode", 0);
            int dId = (int)v.getProperty("dstNode", 0);
            int sCh = (int)v.getProperty("srcCh", 0);
            int dCh = (int)v.getProperty("dstCh", 0);

            auto si = idMap.find(sId);
            auto di = idMap.find(dId);
            if (si == idMap.end() || di == idMap.end()) continue;

            if (engine_.connect(si->second, sCh, di->second, dCh))
                connections_.push_back({ si->second, di->second, sCh, dCh });
        }
    }

    repaint();
}

void GraphEditorComponent::clearSession()
{
    // Remove all nodes from the engine and UI.
    auto ids = descriptors_;
    for (auto& d : ids)
    {
        engine_.removeNode(d.id);
        removeNodeComponent(d.id);
    }
    descriptors_.clear();
    connections_.clear();
    repaint();
}
