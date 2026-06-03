// Shared between kernel driver and user-mode app — must match VirtualMicBridge.h
#pragma once

#define SIGNALLY_SHARED_MEM_NAME   L"\\BaseNamedObjects\\SignallyVirtualMic"
#define SIGNALLY_WRITE_EVENT_NAME  L"\\BaseNamedObjects\\SignallyVMicWrite"

#define SIGNALLY_RING_FRAMES  4096
#define SIGNALLY_CHANNELS     2
#define SIGNALLY_SAMPLE_RATE  48000

#pragma pack(push, 1)
typedef struct _SIGNALLY_SHARED_HEADER
{
    volatile LONG WritePos;   // frames written by user-mode app
    volatile LONG ReadPos;    // frames consumed by driver DMA callback
    ULONG SampleRate;         // active mix/capture sample rate (written by the app)
    ULONG Channels;           // active channel count (written by the app)
    ULONG Reserved[4];        // [0]=allowed PID, [1]=OS output bit depth (16/24/32); see driver.h
} SIGNALLY_SHARED_HEADER, *PSIGNALLY_SHARED_HEADER;
#pragma pack(pop)

// Total shared memory size
#define SIGNALLY_SHARED_MEM_SIZE \
    (sizeof(SIGNALLY_SHARED_HEADER) + \
     SIGNALLY_RING_FRAMES * SIGNALLY_CHANNELS * sizeof(float))
