#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <JuceHeader.h>
#include <atomic>
#include <vector>

// Shared-memory protocol constants — must match VirtualMicDriver/shared_memory.h
static constexpr WCHAR  kSharedMemName[]  = L"Global\\SignallyVirtualMic";
static constexpr WCHAR  kWriteEventName[] = L"Global\\SignallyVMicWrite";
static constexpr int    kSharedRingFrames = 4096; // ring buffer capacity in frames
static constexpr int    kSharedChannels   = 2;
static constexpr int    kSharedSampleRate = 48000;

#pragma pack(push, 1)
struct SharedMicHeader
{
    volatile LONG writePos;  // frames written (atomic, wraps at kSharedRingFrames)
    volatile LONG readPos;   // frames consumed by driver
    DWORD sampleRate;
    DWORD channels;
    DWORD reserved[4];
};
#pragma pack(pop)

// User-mode side: writes processed audio into the shared memory ring buffer
// so the kernel-mode VirtualMicDriver can serve it to WASAPI consumers (e.g. Discord).
class VirtualMicBridge
{
public:
    VirtualMicBridge();
    ~VirtualMicBridge();

    // Returns true if the shared memory was successfully opened (driver installed + running).
    bool open();
    void close();
    bool isOpen() const { return mapping_ != nullptr; }

    // Write planar float audio into the ring buffer.
    // numSamples must be <= kSharedRingFrames.
    void write(float* const* channels, int numChannels, int numSamples);

private:
    HANDLE            mappingHandle_ = nullptr;
    void*             mapping_       = nullptr;
    HANDLE            writeEvent_    = nullptr;

    SharedMicHeader*  header_        = nullptr;
    float*            ringBuffer_    = nullptr; // interleaved, follows header in shared memory
};
