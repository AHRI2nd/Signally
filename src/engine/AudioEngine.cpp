#include "AudioEngine.h"
#include "nodes/InputDeviceNode.h"
#include "nodes/OutputDeviceNode.h"
#include "nodes/VirtualMicOutputNode.h"

#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <combaseapi.h>
#include <timeapi.h>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "winmm.lib")

AudioEngine::AudioEngine()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    mixShutdown_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

AudioEngine::~AudioEngine()
{
    stop();
    if (mixShutdown_) CloseHandle(mixShutdown_);
}

DWORD WINAPI AudioEngine::mixThreadProc(LPVOID param)
{
    static_cast<AudioEngine*>(param)->mixThreadMain();
    return 0;
}

void AudioEngine::mixThreadMain()
{
    // Raise to Pro Audio MMCSS task
    DWORD taskIndex = 0;
    HANDLE mmTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (mmTask) AvSetMmThreadPriority(mmTask, AVRT_PRIORITY_CRITICAL);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // Period in 100ns units derived from block size.
    const double periodMs   = (double)blockSize_ / sampleRate_ * 1000.0;
    LARGE_INTEGER freq;      QueryPerformanceFrequency(&freq);
    LARGE_INTEGER nextWake;  QueryPerformanceCounter(&nextWake);
    const LONGLONG periodTicks = (LONGLONG)(periodMs / 1000.0 * (double)freq.QuadPart);

    while (WaitForSingleObject(mixShutdown_, 0) != WAIT_OBJECT_0)
    {
        LARGE_INTEGER t0; QueryPerformanceCounter(&t0);

        // Run one graph block. InputDeviceNodes read their ring buffers,
        // OutputDeviceNodes write theirs, VirtualMicOutputNode pushes to bridge.
        mixBuffer_.clear();
        mixMidi_.clear();
        graph_.processBlock(mixBuffer_, mixMidi_);

        LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
        double elapsedMs = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart * 1000.0;
        cpuLoad_.store(elapsedMs / periodMs); // fraction of period consumed

        // Sleep until next period boundary (busy-wait the final stretch for accuracy)
        nextWake.QuadPart += periodTicks;
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        if (now.QuadPart < nextWake.QuadPart)
        {
            double remainMs = (double)(nextWake.QuadPart - now.QuadPart)
                            / (double)freq.QuadPart * 1000.0;
            if (remainMs > 1.5)
                Sleep((DWORD)(remainMs - 1.0)); // coarse sleep, leave 1ms for spin
            while (true)
            {
                QueryPerformanceCounter(&now);
                if (now.QuadPart >= nextWake.QuadPart) break;
            }
        }
        else
        {
            // We overran the period — count an underrun and resync.
            underruns_.fetch_add(1);
            nextWake = now;
        }
    }

    if (mmTask) AvRevertMmThreadCharacteristics(mmTask);
}

void AudioEngine::reportError(const juce::String& msg)
{
    if (onError) onError(msg);
}

bool AudioEngine::startDeviceThread(DeviceThread& thread)
{
    if (thread.start())
        return true;

    auto err = thread.lastError();
    reportError(err.isNotEmpty() ? ("Device start failed: " + err)
                                 : "Device start failed");
    return false;
}

double AudioEngine::queryDeviceSampleRate(const std::wstring& deviceId)
{
    if (deviceId.empty())
        return 0.0;

    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
        return 0.0;

    double rate = 0.0;
    IMMDevice* dev = nullptr;
    if (SUCCEEDED(enumerator->GetDevice(deviceId.c_str(), &dev)) && dev)
    {
        IAudioClient* client = nullptr;
        if (SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                    nullptr, reinterpret_cast<void**>(&client))))
        {
            WAVEFORMATEX* mixFmt = nullptr;
            if (SUCCEEDED(client->GetMixFormat(&mixFmt)) && mixFmt)
            {
                rate = static_cast<double>(mixFmt->nSamplesPerSec);
                CoTaskMemFree(mixFmt);
            }
            client->Release();
        }
        dev->Release();
    }
    enumerator->Release();
    return rate;
}

void AudioEngine::applyProcessPriority()
{
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    timeBeginPeriod(1);
    // Disable CPU power throttling
    PROCESS_POWER_THROTTLING_STATE pts{};
    pts.Version    = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    pts.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    pts.StateMask   = 0; // 0 = disable throttling
    SetProcessInformation(GetCurrentProcess(),
                          ProcessPowerThrottling, &pts, sizeof(pts));
}

std::vector<DeviceInfo> AudioEngine::enumerateDevices(DeviceDirection direction)
{
    std::vector<DeviceInfo> result;

    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
        return result;

    EDataFlow flow = (direction == DeviceDirection::Input) ? eCapture : eRender;
    IMMDeviceCollection* collection = nullptr;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)))
    {
        enumerator->Release();
        return result;
    }

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; ++i)
    {
        IMMDevice* dev = nullptr;
        if (FAILED(collection->Item(i, &dev))) continue;

        LPWSTR id = nullptr;
        dev->GetId(&id);

        IPropertyStore* props = nullptr;
        juce::String name;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)))
        {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)))
                name = juce::String(pv.pwszVal);
            PropVariantClear(&pv);
            props->Release();
        }

        // Query max channels via IAudioClient
        IAudioClient* client = nullptr;
        int maxCh = 2;
        if (SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                    nullptr, reinterpret_cast<void**>(&client))))
        {
            WAVEFORMATEX* mixFmt = nullptr;
            if (SUCCEEDED(client->GetMixFormat(&mixFmt)) && mixFmt)
            {
                maxCh = mixFmt->nChannels;
                CoTaskMemFree(mixFmt);
            }
            client->Release();
        }

        result.push_back({ std::wstring(id), name, direction, maxCh });
        CoTaskMemFree(id);
        dev->Release();
    }

    collection->Release();
    enumerator->Release();

    // Append ASIO devices (keyed by name). A single ASIO driver is full-duplex,
    // so the same name may appear in both the input and output lists.
    if (auto asioType = std::unique_ptr<juce::AudioIODeviceType>(
            juce::AudioIODeviceType::createAudioIODeviceType_ASIO()))
    {
        asioType->scanForDevices();
        const bool wantInputs = (direction == DeviceDirection::Input);
        for (auto& nm : asioType->getDeviceNames(wantInputs))
        {
            DeviceInfo di;
            di.id          = std::wstring(nm.toWideCharPointer());
            di.name        = "[ASIO] " + nm;
            di.direction   = direction;
            di.maxChannels = 2;
            di.isAsio      = true;
            result.push_back(std::move(di));
        }
    }

    return result;
}

MixingGraph::NodeID AudioEngine::addInputDevice(const std::wstring& deviceId,
                                                 IsolationMode mode,
                                                 double sampleRate,
                                                 int bufferSamples,
                                                 int numChannels)
{
    DeviceThread::Config cfg;
    cfg.deviceId      = deviceId;
    cfg.direction     = DeviceDirection::Input;
    cfg.isolationMode = mode;
    cfg.sampleRate    = sampleRate;
    cfg.bufferSamples = bufferSamples;
    cfg.numChannels   = numChannels;

    // WASAPI shared devices run at their native mix rate; resample to the engine
    // rate inside the node. Exclusive/ASIO stay at the engine rate (passthrough).
    if (mode == IsolationMode::Shared)
    {
        double devRate = queryDeviceSampleRate(deviceId);
        if (devRate > 0.0) cfg.sampleRate = devRate;
    }

    auto node = std::make_unique<InputDeviceNode>(numChannels);
    InputDeviceNode* nodePtr = node.get();
    nodePtr->setResampling(cfg.sampleRate, sampleRate);

    auto nodeId = graph_.addNode(std::move(node));

    auto thread = std::make_unique<DeviceThread>(cfg,
        [nodePtr](float** ch, int nch, int ns) { nodePtr->pushAudio(ch, nch, ns); });

    std::lock_guard<std::mutex> lock(deviceMutex_);
    if (running_) startDeviceThread(*thread);
    deviceThreads_.push_back(std::move(thread));

    return nodeId;
}

MixingGraph::NodeID AudioEngine::addOutputDevice(const std::wstring& deviceId,
                                                   IsolationMode mode,
                                                   double sampleRate,
                                                   int bufferSamples,
                                                   int numChannels)
{
    DeviceThread::Config cfg;
    cfg.deviceId      = deviceId;
    cfg.direction     = DeviceDirection::Output;
    cfg.isolationMode = mode;
    cfg.sampleRate    = sampleRate;
    cfg.bufferSamples = bufferSamples;
    cfg.numChannels   = numChannels;

    // WASAPI shared devices run at their native mix rate; the node resamples the
    // engine-rate audio to it. Exclusive/ASIO stay at the engine rate (passthrough).
    if (mode == IsolationMode::Shared)
    {
        double devRate = queryDeviceSampleRate(deviceId);
        if (devRate > 0.0) cfg.sampleRate = devRate;
    }

    auto node = std::make_unique<OutputDeviceNode>(numChannels);
    OutputDeviceNode* nodePtr = node.get();
    nodePtr->setResampling(cfg.sampleRate, sampleRate);

    auto nodeId = graph_.addNode(std::move(node));

    auto thread = std::make_unique<DeviceThread>(cfg,
        [nodePtr](float** ch, int nch, int ns) { nodePtr->pullAudio(ch, nch, ns); });

    std::lock_guard<std::mutex> lock(deviceMutex_);
    if (running_) startDeviceThread(*thread);
    deviceThreads_.push_back(std::move(thread));

    return nodeId;
}

MixingGraph::NodeID AudioEngine::addVirtualMicOutput(int numChannels)
{
    auto node = std::make_unique<VirtualMicOutputNode>(vmicBridge_, numChannels);
    return graph_.addNode(std::move(node));
}

void AudioEngine::removeNode(MixingGraph::NodeID id)
{
    graph_.disconnectAll(id);
    graph_.removeNode(id);
}

bool AudioEngine::connect(MixingGraph::NodeID src, int srcCh,
                           MixingGraph::NodeID dst, int dstCh)
{
    return graph_.connect(src, srcCh, dst, dstCh);
}

bool AudioEngine::disconnect(MixingGraph::NodeID src, int srcCh,
                              MixingGraph::NodeID dst, int dstCh)
{
    return graph_.disconnect(src, srcCh, dst, dstCh);
}

bool AudioEngine::start()
{
    if (running_) return true;
    applyProcessPriority();

    if (!vmicBridge_.open())
    {
        // Not fatal — virtual mic may not be installed yet
        juce::Logger::writeToLog("VirtualMicBridge: driver not found, virtual mic disabled");
    }
    else
    {
        // Publish the active format so the driver exposes a matching capture
        // format. Mix rate == virtual-mic rate (no resampling on this path).
        vmicBridge_.setFormat(static_cast<int>(sampleRate_), micBitDepth_);
    }

    graph_.prepare(sampleRate_, blockSize_);

    // Allocate a worst-case mix buffer. The graph routes audio between nodes'
    // internal ring buffers, so the top-level buffer just needs a stereo block.
    mixBuffer_.setSize(2, blockSize_, false, true, true);

    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        // Start every device; a failure on one device (e.g. a mic already held
        // exclusively by another app) is surfaced via onError but does not stop
        // the remaining devices from running.
        for (auto& t : deviceThreads_)
            startDeviceThread(*t);
    }

    // Start the graph-driving mix thread last, once devices are feeding rings.
    ResetEvent(mixShutdown_);
    underruns_.store(0);
    mixThread_ = CreateThread(nullptr, 0, mixThreadProc, this, 0, nullptr);

    running_ = true;
    return true;
}

void AudioEngine::stop()
{
    if (!running_) return;

    // Stop the mix thread first so the graph isn't processed while devices tear down.
    SetEvent(mixShutdown_);
    if (mixThread_)
    {
        WaitForSingleObject(mixThread_, 5000);
        CloseHandle(mixThread_);
        mixThread_ = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        for (auto& t : deviceThreads_)
            t->stop();
    }
    graph_.release();
    vmicBridge_.close();
    running_ = false;
    timeEndPeriod(1);
}
