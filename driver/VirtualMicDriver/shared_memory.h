// Shared between kernel driver and user-mode app — must match VirtualMicBridge.h
#pragma once

#define MICTRANS_SHARED_MEM_NAME   L"\\BaseNamedObjects\\MicTransVirtualMic"
#define MICTRANS_WRITE_EVENT_NAME  L"\\BaseNamedObjects\\MicTransVMicWrite"

#define MICTRANS_RING_FRAMES  4096
#define MICTRANS_CHANNELS     2
#define MICTRANS_SAMPLE_RATE  48000

#pragma pack(push, 1)
typedef struct _MICTRANS_SHARED_HEADER
{
    volatile LONG WritePos;   // frames written by user-mode app
    volatile LONG ReadPos;    // frames consumed by driver DMA callback
    ULONG SampleRate;
    ULONG Channels;
    ULONG Reserved[4];
} MICTRANS_SHARED_HEADER, *PMICTRANS_SHARED_HEADER;
#pragma pack(pop)

// Total shared memory size
#define MICTRANS_SHARED_MEM_SIZE \
    (sizeof(MICTRANS_SHARED_HEADER) + \
     MICTRANS_RING_FRAMES * MICTRANS_CHANNELS * sizeof(float))
