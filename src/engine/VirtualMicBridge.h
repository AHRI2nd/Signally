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
static constexpr int    kSharedSampleRate = 48000; // default; actual rate is written to the header at runtime

// reserved[] slot assignments (must match driver/VirtualMicDriver/shared_memory.h).
//   [0] = allowed consumer PID (0 = any)
//   [1] = virtual-mic OS-exposed output bit depth (16 / 24 / 32). The ring is
//         always float32; the kernel driver converts to this depth when serving
//         the capture pin.
static constexpr int    kReservedPidIndex      = 0;
static constexpr int    kReservedBitDepthIndex = 1;

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

    // Publish the active audio format to the shared header so the kernel driver
    // exposes the matching KS capture format. The mix ring is always float32 at
    // `sampleRate`; `bitDepth` (16/24/32) is the OS-exposed output depth.
    // Call after open() and whenever the app's format changes.
    void setFormat(int sampleRate, int bitDepth);

    // Write planar float audio into the ring buffer.
    // numSamples must be <= kSharedRingFrames.
    void write(float* const* channels, int numChannels, int numSamples);

    // Restrict which process may open the virtual-mic capture pin.
    //   0 (default) → any consumer (Discord, etc.) may open it.
    //   non-zero    → only that PID may open it; the driver denies all others.
    // Stored in the shared header's reserved[0], read by the kernel driver.
    void setAllowedConsumerPid(unsigned long pid);

private:
    HANDLE            mappingHandle_ = nullptr;
    void*             mapping_       = nullptr;
    HANDLE            writeEvent_    = nullptr;

    SharedMicHeader*  header_        = nullptr;
    float*            ringBuffer_    = nullptr; // interleaved, follows header in shared memory
    int               bitDepth_      = 32;      // OS-exposed output depth; encode ring to this
};
