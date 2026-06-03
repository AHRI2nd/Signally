#include "DeviceThread.h"
#include <functiondiscoverykeys_devpkey.h>
#include <cstring>

// Reference time unit: 100-nanosecond intervals
static constexpr REFERENCE_TIME kRefTime100ns = 10000000LL;

static REFERENCE_TIME samplesToRefTime(int samples, double sampleRate)
{
    return static_cast<REFERENCE_TIME>(
        static_cast<double>(samples) / sampleRate * static_cast<double>(kRefTime100ns));
}

DeviceThread::DeviceThread(Config config, AudioCallback callback)
    : config_(std::move(config)), callback_(std::move(callback))
{
    staging_.resize(static_cast<size_t>(config_.bufferSamples * config_.numChannels), 0.0f);
    shutdownEvt_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bufferEvt_   = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

DeviceThread::~DeviceThread()
{
    stop();
    if (shutdownEvt_) CloseHandle(shutdownEvt_);
    if (bufferEvt_)   CloseHandle(bufferEvt_);
}

bool DeviceThread::start()
{
    if (running_.load()) return true;

    if (config_.isolationMode == IsolationMode::ASIO)
        return startASIO();

    if (!initWASAPI())
        return false;

    ResetEvent(shutdownEvt_);
    running_.store(true);
    thread_ = CreateThread(nullptr, 0, threadProc, this, 0, nullptr);
    if (!thread_)
    {
        running_.store(false);
        releaseWASAPI();
        lastError_ = "CreateThread failed";
        return false;
    }
    return true;
}

void DeviceThread::stop()
{
    if (!running_.load()) return;

    if (asioDevice_ != nullptr)
    {
        stopASIO();
        running_.store(false);
        return;
    }

    SetEvent(shutdownEvt_);
    if (thread_)
    {
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    running_.store(false);
    releaseWASAPI();
}

// ── ASIO backend ──────────────────────────────────────────────────────────────

bool DeviceThread::startASIO()
{
    asioType_.reset(juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
    if (asioType_ == nullptr)
    {
        lastError_ = "ASIO support is not built into this binary";
        return false;
    }
    asioType_->scanForDevices();

    const bool   isInput = (config_.direction == DeviceDirection::Input);
    juce::String devName(config_.deviceId.c_str()); // ASIO devices are keyed by name

    // For an output node create (outputName, ""), for input ("", inputName).
    asioDevice_.reset(asioType_->createDevice(isInput ? juce::String() : devName,
                                              isInput ? devName : juce::String()));
    if (asioDevice_ == nullptr)
    {
        lastError_ = "ASIO device not found: " + devName;
        asioType_.reset();
        return false;
    }

    juce::BigInteger inChannels, outChannels;
    if (isInput) inChannels .setRange(0, config_.numChannels, true);
    else         outChannels.setRange(0, config_.numChannels, true);

    juce::String err = asioDevice_->open(inChannels, outChannels,
                                         config_.sampleRate, config_.bufferSamples);
    if (err.isNotEmpty())
    {
        lastError_ = "ASIO open failed: " + err;
        asioDevice_.reset();
        asioType_.reset();
        return false;
    }

    running_.store(true);
    asioDevice_->start(this); // JUCE runs its own real-time thread
    return true;
}

void DeviceThread::stopASIO()
{
    if (asioDevice_ != nullptr)
    {
        asioDevice_->stop();
        asioDevice_->close();
        asioDevice_.reset();
    }
    asioType_.reset();
}

void DeviceThread::audioDeviceAboutToStart(juce::AudioIODevice*)
{
    asioChanPtrs_.assign(static_cast<size_t>(config_.numChannels), nullptr);
}

void DeviceThread::audioDeviceStopped() {}

void DeviceThread::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    if (config_.direction == DeviceDirection::Input)
    {
        const int ch = juce::jmin(numInputChannels, config_.numChannels);
        if (static_cast<int>(asioChanPtrs_.size()) < ch)
            asioChanPtrs_.resize(static_cast<size_t>(ch));
        for (int c = 0; c < ch; ++c)
            asioChanPtrs_[c] = const_cast<float*>(inputChannelData[c]); // pushAudio only reads
        if (callback_ && ch > 0)
            callback_(asioChanPtrs_.data(), ch, numSamples);
    }
    else
    {
        // Start from silence so unused channels render clean.
        for (int c = 0; c < numOutputChannels; ++c)
            if (outputChannelData[c] != nullptr)
                juce::FloatVectorOperations::clear(outputChannelData[c], numSamples);

        const int ch = juce::jmin(numOutputChannels, config_.numChannels);
        if (callback_ && ch > 0)
            callback_(const_cast<float**>(outputChannelData), ch, numSamples);
    }
}

DWORD WINAPI DeviceThread::threadProc(LPVOID param)
{
    static_cast<DeviceThread*>(param)->audioThreadMain();
    return 0;
}

void DeviceThread::audioThreadMain()
{
    // Raise to Pro Audio MMCSS task
    DWORD taskIndex = 0;
    HANDLE mmTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (mmTask) AvSetMmThreadPriority(mmTask, AVRT_PRIORITY_CRITICAL);

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    HRESULT hr = client_->Start();
    if (FAILED(hr)) { running_.store(false); return; }

    const HANDLE events[2] = { shutdownEvt_, bufferEvt_ };

    while (true)
    {
        DWORD wait = WaitForMultipleObjects(2, events, FALSE, 200);
        if (wait == WAIT_OBJECT_0) break; // shutdown

        if (config_.direction == DeviceDirection::Input)
        {
            BYTE*  data   = nullptr;
            UINT32 frames = 0;
            DWORD  flags  = 0;

            while (SUCCEEDED(captureClient_->GetNextPacketSize(&frames)) && frames > 0)
            {
                if (FAILED(captureClient_->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                    break;

                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && callback_)
                {
                    // De-interleave into staging_
                    auto* src = reinterpret_cast<const float*>(data);
                    int ch = config_.numChannels;
                    staging_.assign(src, src + frames * ch);

                    // Build channel pointer array
                    std::vector<float*> ptrs(ch);
                    for (int c = 0; c < ch; ++c)
                        ptrs[c] = staging_.data() + c * frames;

                    // Re-arrange interleaved → planar
                    std::vector<float> planar(frames * ch);
                    for (int f = 0; f < (int)frames; ++f)
                        for (int c = 0; c < ch; ++c)
                            planar[c * frames + f] = src[f * ch + c];
                    for (int c = 0; c < ch; ++c)
                        ptrs[c] = planar.data() + c * frames;

                    callback_(ptrs.data(), ch, (int)frames);
                }
                captureClient_->ReleaseBuffer(frames);
            }
        }
        else // Output
        {
            UINT32 bufferFrames = 0, padding = 0;
            client_->GetBufferSize(&bufferFrames);
            client_->GetCurrentPadding(&padding);
            UINT32 available = bufferFrames - padding;
            if (available == 0) continue;

            BYTE* data = nullptr;
            if (FAILED(renderClient_->GetBuffer(available, &data))) continue;

            int ch = config_.numChannels;
            std::vector<float> planar(available * ch, 0.0f);
            std::vector<float*> ptrs(ch);
            for (int c = 0; c < ch; ++c) ptrs[c] = planar.data() + c * available;

            if (callback_) callback_(ptrs.data(), ch, (int)available);

            // Interleave planar → WASAPI buffer
            auto* dst = reinterpret_cast<float*>(data);
            for (int f = 0; f < (int)available; ++f)
                for (int c = 0; c < ch; ++c)
                    dst[f * ch + c] = planar[c * available + f];

            renderClient_->ReleaseBuffer(available, 0);
        }
    }

    client_->Stop();
    if (mmTask) AvRevertMmThreadCharacteristics(mmTask);
}

bool DeviceThread::initWASAPI()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) { lastError_ = "CoCreateInstance(MMDeviceEnumerator) failed"; return false; }

    if (config_.deviceId.empty())
    {
        EDataFlow flow = (config_.direction == DeviceDirection::Input) ? eCapture : eRender;
        hr = enumerator->GetDefaultAudioEndpoint(flow, eCommunications, &device_);
    }
    else
    {
        hr = enumerator->GetDevice(config_.deviceId.c_str(), &device_);
    }
    enumerator->Release();

    if (FAILED(hr)) { lastError_ = "GetAudioEndpoint failed"; return false; }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(&client_));
    if (FAILED(hr)) { lastError_ = "IAudioClient Activate failed"; return false; }

    WAVEFORMATEX fmt{};
    fmt.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels       = static_cast<WORD>(config_.numChannels);
    fmt.nSamplesPerSec  = static_cast<DWORD>(config_.sampleRate);
    fmt.wBitsPerSample  = 32;
    fmt.nBlockAlign     = fmt.nChannels * (fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize          = 0;

    REFERENCE_TIME period = samplesToRefTime(config_.bufferSamples, config_.sampleRate);

    AUDCLNT_SHAREMODE shareMode = (config_.isolationMode == IsolationMode::Exclusive)
                                   ? AUDCLNT_SHAREMODE_EXCLUSIVE
                                   : AUDCLNT_SHAREMODE_SHARED;

    // Pro interfaces often accept only 24-bit (not 32-bit float) in Exclusive mode,
    // which makes Initialize fail and the device never opens. If our format isn't
    // supported exclusively, fall back to Shared, where the audio engine converts.
    if (shareMode == AUDCLNT_SHAREMODE_EXCLUSIVE
        && client_->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &fmt, nullptr) != S_OK)
    {
        shareMode = AUDCLNT_SHAREMODE_SHARED;
    }

    DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (shareMode == AUDCLNT_SHAREMODE_SHARED)
    {
        // Let the Windows audio engine convert between our float/48k request and
        // the device's actual mix format (bit depth, channel mask, sample rate)
        // so shared-mode Initialize succeeds even when the formats differ.
        streamFlags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                     | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    }

    hr = client_->Initialize(shareMode, streamFlags, period,
                              (shareMode == AUDCLNT_SHAREMODE_EXCLUSIVE) ? period : 0,
                              &fmt, nullptr);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED && shareMode == AUDCLNT_SHAREMODE_EXCLUSIVE)
    {
        // Retry with aligned buffer size
        UINT32 aligned = 0;
        client_->GetBufferSize(&aligned);
        period = samplesToRefTime(aligned, config_.sampleRate);
        hr = client_->Initialize(shareMode, streamFlags, period, period, &fmt, nullptr);
    }
    if (FAILED(hr))
    {
        lastError_ = juce::String("IAudioClient::Initialize failed: 0x") +
                     juce::String::toHexString((int)hr);
        return false;
    }

    client_->SetEventHandle(bufferEvt_);

    if (config_.direction == DeviceDirection::Input)
        hr = client_->GetService(IID_PPV_ARGS(&captureClient_));
    else
        hr = client_->GetService(IID_PPV_ARGS(&renderClient_));

    if (FAILED(hr)) { lastError_ = "GetService failed"; return false; }

    return true;
}

void DeviceThread::releaseWASAPI()
{
    if (captureClient_) { captureClient_->Release(); captureClient_ = nullptr; }
    if (renderClient_)  { renderClient_->Release();  renderClient_  = nullptr; }
    if (client_)        { client_->Release();         client_        = nullptr; }
    if (device_)        { device_->Release();          device_        = nullptr; }
}
