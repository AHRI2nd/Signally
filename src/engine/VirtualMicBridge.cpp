#include "VirtualMicBridge.h"

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

void VirtualMicBridge::write(float* const* channels, int numChannels, int numSamples)
{
    if (!header_ || !ringBuffer_) return;

    int ch = (numChannels < kSharedChannels) ? numChannels : kSharedChannels;

    for (int i = 0; i < numSamples; ++i)
    {
        LONG wp = InterlockedAdd(&header_->writePos, 0) % kSharedRingFrames;
        int base = wp * kSharedChannels;
        for (int c = 0; c < ch; ++c)
            ringBuffer_[base + c] = channels[c][i];
        // mono up-mix
        for (int c = ch; c < kSharedChannels; ++c)
            ringBuffer_[base + c] = (ch > 0) ? channels[0][i] : 0.0f;

        InterlockedIncrement(&header_->writePos);
    }

    if (writeEvent_) SetEvent(writeEvent_);
}

void VirtualMicBridge::setAllowedConsumerPid(unsigned long pid)
{
    // reserved[0] is the agreed slot for the access-control PID (see driver.h).
    if (header_) header_->reserved[0] = static_cast<DWORD>(pid);
}
