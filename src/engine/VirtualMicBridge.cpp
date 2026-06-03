#include "VirtualMicBridge.h"
#include <cstdint>

static constexpr SIZE_T kSharedMemSize =
    sizeof(SharedMicHeader) +
    kSharedRingFrames * kSharedChannels * sizeof(float);

VirtualMicBridge::VirtualMicBridge() = default;

VirtualMicBridge::~VirtualMicBridge()
{
    close();
}

bool VirtualMicBridge::open()
{
    close();

    // The kernel driver creates the shared memory; we open it.
    mappingHandle_ = OpenFileMappingW(FILE_MAP_WRITE, FALSE, kSharedMemName);
    if (!mappingHandle_)
        return false;

    mapping_ = MapViewOfFile(mappingHandle_, FILE_MAP_WRITE, 0, 0, kSharedMemSize);
    if (!mapping_)
    {
        CloseHandle(mappingHandle_);
        mappingHandle_ = nullptr;
        return false;
    }

    header_     = static_cast<SharedMicHeader*>(mapping_);
    ringBuffer_ = reinterpret_cast<float*>(header_ + 1);

    writeEvent_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, kWriteEventName);
    return true;
}

void VirtualMicBridge::close()
{
    if (writeEvent_) { CloseHandle(writeEvent_); writeEvent_ = nullptr; }
    if (mapping_)    { UnmapViewOfFile(mapping_); mapping_ = nullptr; header_ = nullptr; ringBuffer_ = nullptr; }
    if (mappingHandle_) { CloseHandle(mappingHandle_); mappingHandle_ = nullptr; }
}

// Encode one float sample [-1,1] into the OS-exposed depth at `dst`.
// All FP work happens here in user mode, so the kernel driver only copies bytes.
static inline void encodeSample(BYTE* dst, float v, int bitDepth)
{
    v = juce::jlimit(-1.0f, 1.0f, v);
    switch (bitDepth)
    {
        case 16:
        {
            int16_t s = static_cast<int16_t>(juce::roundToInt(v * 32767.0f));
            memcpy(dst, &s, 2);
            break;
        }
        case 24:
        {
            int32_t s = juce::roundToInt(v * 8388607.0f); // 2^23 - 1
            dst[0] = static_cast<BYTE>(s & 0xFF);
            dst[1] = static_cast<BYTE>((s >> 8) & 0xFF);
            dst[2] = static_cast<BYTE>((s >> 16) & 0xFF);
            break;
        }
        default: // 32-bit IEEE float passthrough
            memcpy(dst, &v, 4);
            break;
    }
}

void VirtualMicBridge::write(float* const* channels, int numChannels, int numSamples)
{
    if (!header_ || !ringBuffer_) return;

    const int ch             = (numChannels < kSharedChannels) ? numChannels : kSharedChannels;
    const int bytesPerSample = bitDepth_ / 8;
    const int frameBytes     = kSharedChannels * bytesPerSample;
    BYTE*     ring           = reinterpret_cast<BYTE*>(ringBuffer_);

    for (int i = 0; i < numSamples; ++i)
    {
        LONG  wp    = InterlockedAdd(&header_->writePos, 0) % kSharedRingFrames;
        BYTE* frame = ring + static_cast<SIZE_T>(wp) * frameBytes;

        for (int c = 0; c < kSharedChannels; ++c)
        {
            // mono up-mix: duplicate channel 0 into the extra channels.
            float v = (c < ch) ? channels[c][i] : ((ch > 0) ? channels[0][i] : 0.0f);
            encodeSample(frame + c * bytesPerSample, v, bitDepth_);
        }

        InterlockedIncrement(&header_->writePos);
    }

    if (writeEvent_) SetEvent(writeEvent_);
}

void VirtualMicBridge::setFormat(int sampleRate, int bitDepth)
{
    bitDepth_ = bitDepth;            // used by write() even before the driver attaches
    if (!header_) return;
    header_->sampleRate = static_cast<DWORD>(sampleRate);
    header_->channels   = static_cast<DWORD>(kSharedChannels);
    header_->reserved[kReservedBitDepthIndex] = static_cast<DWORD>(bitDepth);
}

void VirtualMicBridge::setAllowedConsumerPid(unsigned long pid)
{
    // reserved[kReservedPidIndex] is the agreed slot for the access-control PID.
    if (header_) header_->reserved[kReservedPidIndex] = static_cast<DWORD>(pid);
}
