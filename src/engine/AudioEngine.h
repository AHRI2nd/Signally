#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>

#include "DeviceThread.h"
#include "MixingGraph.h"
#include "VirtualMicBridge.h"

#include <JuceHeader.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <functional>

// Descriptor of an audio device visible to the engine.
struct DeviceInfo
{
    std::wstring id;
    juce::String name;
    DeviceDirection direction;
    int maxChannels;
};

// AudioEngine owns all DeviceThreads, the MixingGraph, and the VirtualMicBridge.
// It is the central coordinator: threads push/pull audio into a shared ring-buffer
// set, and a dedicated mix thread runs the graph.
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    // Enumerate available devices.
    std::vector<DeviceInfo> enumerateDevices(DeviceDirection direction);

    // Add an input device node. Returns the graph NodeID assigned.
    MixingGraph::NodeID addInputDevice(const std::wstring& deviceId,
                                       IsolationMode mode  = IsolationMode::Exclusive,
                                       double sampleRate   = 48000.0,
                                       int    bufferSamples = 480,
                                       int    numChannels   = 2);

    // Add an output device node.
    MixingGraph::NodeID addOutputDevice(const std::wstring& deviceId,
                                        IsolationMode mode  = IsolationMode::Shared,
                                        double sampleRate   = 48000.0,
                                        int    bufferSamples = 480,
                                        int    numChannels   = 2);

    // Add a virtual microphone output node (feeds VirtualMicBridge).
    MixingGraph::NodeID addVirtualMicOutput(int numChannels = 2);

    // Remove any device or node.
    void removeNode(MixingGraph::NodeID id);

    // Connect / disconnect graph nodes.
    bool connect(MixingGraph::NodeID src, int srcCh,
                 MixingGraph::NodeID dst, int dstCh);
    bool disconnect(MixingGraph::NodeID src, int srcCh,
                    MixingGraph::NodeID dst, int dstCh);

    MixingGraph& graph() { return graph_; }

    // Start / stop all device threads.
    bool start();
    void stop();
    bool isRunning() const { return running_; }

    VirtualMicBridge& virtualMicBridge() { return vmicBridge_; }

    // Listener called on the message thread for errors.
    std::function<void(const juce::String&)> onError;

    // Performance counters (read from UI thread).
    double getCpuLoad()      const { return cpuLoad_.load(); }
    int    getUnderrunCount()const { return underruns_.load(); }

private:
    void applyProcessPriority();

    // Dedicated real-time thread that drives the graph: it pulls captured audio
    // from InputDeviceNodes (via their ring buffers) and pushes processed audio
    // into OutputDeviceNodes — one processBlock() per period.
    static DWORD WINAPI mixThreadProc(LPVOID param);
    void mixThreadMain();

    MixingGraph            graph_;
    VirtualMicBridge       vmicBridge_;

    std::vector<std::unique_ptr<DeviceThread>> deviceThreads_;
    std::mutex                                 deviceMutex_;

    // Mix thread
    HANDLE              mixThread_     = nullptr;
    HANDLE              mixShutdown_   = nullptr;
    double              sampleRate_    = 48000.0;
    int                 blockSize_     = 480;
    juce::AudioBuffer<float> mixBuffer_;
    juce::MidiBuffer         mixMidi_;

    std::atomic<double> cpuLoad_  { 0.0 };
    std::atomic<int>    underruns_{ 0 };

    bool running_ = false;
};
