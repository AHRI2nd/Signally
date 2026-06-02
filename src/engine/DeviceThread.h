#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <string>

// Isolation mode for an output device endpoint.
enum class IsolationMode { Shared, Exclusive };

// Direction of this device thread.
enum class DeviceDirection { Input, Output };

// Callback fired on the audio thread with captured or consumed audio.
// Input:  buffer contains captured samples (read-only in callback).
// Output: callback must fill buffer with samples to render.
using AudioCallback = std::function<void(float** channels, int numChannels, int numSamples)>;

// Manages a single WASAPI device endpoint on a dedicated real-time thread.
// For Input: Exclusive capture or Shared capture.
// For Output: Shared Low Latency render or Exclusive render.
class DeviceThread
{
public:
    struct Config
    {
        std::wstring  deviceId;
        DeviceDirection direction      = DeviceDirection::Input;
        IsolationMode   isolationMode  = IsolationMode::Shared;
        double          sampleRate     = 48000.0;
        int             bufferSamples  = 480;   // 10ms @ 48kHz
        int             numChannels    = 2;
    };

    explicit DeviceThread(Config config, AudioCallback callback);
    ~DeviceThread();

    bool start();
    void stop();

    bool    isRunning()    const { return running_.load(); }
    double  sampleRate()   const { return config_.sampleRate; }
    int     numChannels()  const { return config_.numChannels; }
    int     bufferSamples()const { return config_.bufferSamples; }

    // Last reported error (empty if none)
    juce::String lastError() const { return lastError_; }

private:
    static DWORD WINAPI threadProc(LPVOID param);
    void audioThreadMain();

    bool initWASAPI();
    void releaseWASAPI();

    Config         config_;
    AudioCallback  callback_;

    HANDLE         thread_      = nullptr;
    HANDLE         shutdownEvt_ = nullptr;
    HANDLE         bufferEvt_   = nullptr;

    IMMDevice*     device_      = nullptr;
    IAudioClient*  client_      = nullptr;
    IAudioCaptureClient* captureClient_ = nullptr;
    IAudioRenderClient*  renderClient_  = nullptr;

    std::atomic<bool>  running_{ false };
    juce::String       lastError_;

    // Interleaved staging buffer shared between WASAPI and callback
    std::vector<float> staging_;
};
