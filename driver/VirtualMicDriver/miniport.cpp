// miniport.cpp — WaveCyclic miniport: implements the virtual capture pin.
// The DMA callback reads from the shared-memory ring buffer written by the app.

#include "driver.h"
#include <msvad.h>   // for MSVAD_CYCLIC_BUFFER_SIZE (WDK sample header)

// PCM format exposed to the OS: 48000Hz, 32-bit float, stereo
static KSDATAFORMAT_WAVEFORMATEX gCaptureFormat =
{
    {
        sizeof(KSDATAFORMAT_WAVEFORMATEX),
        0,
        sizeof(WAVEFORMATEX),
        { STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO) },
        { STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) },
        { STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) }
    },
    {
        WAVE_FORMAT_IEEE_FLOAT,
        SIGNALLY_CHANNELS,
        SIGNALLY_SAMPLE_RATE,
        SIGNALLY_SAMPLE_RATE * SIGNALLY_CHANNELS * sizeof(float),
        SIGNALLY_CHANNELS * sizeof(float),
        32,
        0
    }
};

// ── CMiniportWaveCyclicSignally ───────────────────────────────────────────────
class CMiniportWaveCyclicSignally
    : public IMiniportWaveCyclic,
      public CUnknown
{
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportWaveCyclicSignally);

public:
    // IMiniport
    STDMETHODIMP_(NTSTATUS) GetDescription(_Out_ PPCFILTER_DESCRIPTOR* desc) override;
    STDMETHODIMP_(NTSTATUS) DataRangeIntersection(
        ULONG, PKSDATARANGE, PKSDATARANGE, ULONG, PVOID, PULONG) override;

    // IMiniportWaveCyclic
    STDMETHODIMP_(NTSTATUS) Init(
        PUNKNOWN, PRESOURCELIST, PPORTWAVECYCLIC, PSERVICEGROUP*) override;
    STDMETHODIMP_(NTSTATUS) NewStream(
        PMINIPORTWAVECYCLICSTREAM*, PUNKNOWN, POOL_TYPE,
        ULONG, BOOLEAN, PKSDATAFORMAT, PDMACHANNEL*, PSERVICEGROUP*) override;

private:
    PPORTWAVECYCLIC Port_ = nullptr;
};

// ── CMiniportStreamSignally ───────────────────────────────────────────────────
class CMiniportStreamSignally
    : public IMiniportWaveCyclicStream,
      public CUnknown
{
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportStreamSignally);
    ~CMiniportStreamSignally();

public:
    STDMETHODIMP_(NTSTATUS) Init(
        CMiniportWaveCyclicSignally*, ULONG, BOOLEAN, PKSDATAFORMAT) ;

    // IMiniportWaveCyclicStream
    STDMETHODIMP_(NTSTATUS) GetPosition(_Out_ PULONG pos) override;
    STDMETHODIMP_(NTSTATUS) NormalizePhysicalPosition(PLONGLONG pos) override;
    STDMETHODIMP_(NTSTATUS) SetFormat(PKSDATAFORMAT) override;
    STDMETHODIMP_(ULONG)    SetNotificationFreq(ULONG interval, PULONG framing) override;
    STDMETHODIMP_(NTSTATUS) SetState(KSSTATE state) override;
    STDMETHODIMP_(void)     Silence(PVOID buf, ULONG byteCount) override;

    // DMA buffer fill — called by PortCls at each notification
    void ServiceGroup();

private:
    CMiniportWaveCyclicSignally* Miniport_ = nullptr;
    ULONG    Channel_          = 0;
    PVOID    DmaBuffer_        = nullptr;
    ULONG    DmaBufferSize_    = 0;
    ULONG    DmaPosition_      = 0;
    KSSTATE  State_            = KSSTATE_STOP;
    ULONG    NotificationFreq_ = 10; // ms
};

// ── Factory ───────────────────────────────────────────────────────────────────

NTSTATUS
CreateMiniport(
    _Out_ PUNKNOWN* Unknown,
    _In_  REFCLSID  ClassID,
    _In_  PUNKNOWN  UnknownOuter OPTIONAL,
    _In_  POOL_TYPE PoolType)
{
    UNREFERENCED_PARAMETER(ClassID);

    *Unknown = new(PoolType, 'cmiM') CMiniportWaveCyclicSignally(UnknownOuter);
    if (!*Unknown) return STATUS_INSUFFICIENT_RESOURCES;
    (*Unknown)->AddRef();
    return STATUS_SUCCESS;
}

// ── CMiniportWaveCyclicSignally impl ─────────────────────────────────────────

STDMETHODIMP_(NTSTATUS)
CMiniportWaveCyclicSignally::Init(
    PUNKNOWN        UnknownAdapter,
    PRESOURCELIST   ResourceList,
    PPORTWAVECYCLIC Port,
    PSERVICEGROUP*  ServiceGroup)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(ServiceGroup);

    Port_ = Port;
    Port_->AddRef();
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveCyclicSignally::NewStream(
    PMINIPORTWAVECYCLICSTREAM* Stream,
    PUNKNOWN                   OuterUnknown,
    POOL_TYPE                  PoolType,
    ULONG                      Pin,
    BOOLEAN                    Capture,
    PKSDATAFORMAT              DataFormat,
    PDMACHANNEL*               DmaChannel,
    PSERVICEGROUP*             ServiceGroup)
{
    if (!Capture) return STATUS_INVALID_PARAMETER;

    // Optional consumer allow-list: if the app advertised a specific PID, deny
    // every other process that tries to open the capture pin (max isolation).
    if (g_SignallyShared.Header != nullptr)
    {
        LONG allowed = (LONG) g_SignallyShared.Header->Reserved[SIGNALLY_ALLOWED_PID_INDEX];
        if (allowed != 0 && (LONG)(ULONG_PTR) PsGetCurrentProcessId() != allowed)
            return STATUS_ACCESS_DENIED;
    }

    auto* stream = new(PoolType, 'rtSM') CMiniportStreamSignally(OuterUnknown);
    if (!stream) return STATUS_INSUFFICIENT_RESOURCES;

    // Init allocates the cyclic DMA buffer the OS will read captured audio from.
    NTSTATUS status = stream->Init(this, Pin, Capture, DataFormat);
    if (!NT_SUCCESS(status)) { stream->Release(); return status; }

    // NOTE: a production WaveCyclic miniport returns an IDmaChannel whose buffer
    // IS the cyclic capture buffer (see WDK 'msvad'/'sysvad'). Here the stream
    // owns the buffer directly; *DmaChannel stays null (software-emulated DMA).
    *DmaChannel = nullptr;

    // Service group drives periodic ServiceGroup() notifications.
    status = PcNewServiceGroup(ServiceGroup, nullptr);
    if (!NT_SUCCESS(status)) { stream->Release(); return status; }

    *Stream = stream;
    return STATUS_SUCCESS;
}

// Supported capture data range: PCM IEEE-float, 48 kHz, stereo, 32-bit.
static KSDATARANGE_AUDIO gCaptureDataRange =
{
    {
        sizeof(KSDATARANGE_AUDIO),
        0, 0, 0,
        { STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO) },
        { STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) },
        { STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) }
    },
    SIGNALLY_CHANNELS,           // MaximumChannels
    32, 32,                      // Min/Max bits per sample
    SIGNALLY_SAMPLE_RATE,        // MinimumSampleFrequency
    SIGNALLY_SAMPLE_RATE         // MaximumSampleFrequency
};
static PKSDATARANGE gCaptureDataRanges[] = { (PKSDATARANGE)&gCaptureDataRange };

// The capture pin's KS interface/category GUIDs (KSCATEGORY_AUDIO + _CAPTURE).
static GUID gPinCategory_Capture = { STATIC_KSCATEGORY_CAPTURE };
static GUID gPinCategory_Audio   = { STATIC_KSCATEGORY_AUDIO };

STDMETHODIMP_(NTSTATUS)
CMiniportWaveCyclicSignally::GetDescription(_Out_ PPCFILTER_DESCRIPTOR* desc)
{
    // One capture (source) pin exposing the float/48k/stereo data range.
    static PCPIN_DESCRIPTOR pinDesc[] =
    {
        {
            1, 1, 0,            // MaxGlobalInstanceCount, MaxFilterInstanceCount, MinFilterInstanceCount
            nullptr,            // AutomationTable
            {
                0, nullptr,                                   // Interfaces (filled by PortCls)
                0, nullptr,                                   // Mediums
                SIZEOF_ARRAY(gCaptureDataRanges),             // DataRangesCount
                gCaptureDataRanges,                           // DataRanges
                KSPIN_DATAFLOW_OUT,                           // capture = data flows OUT of the pin
                KSPIN_COMMUNICATION_SINK,
                &gPinCategory_Capture,                        // Category
                nullptr,                                      // Name
                0                                             // Reserved
            }
        }
    };

    static PCFILTER_DESCRIPTOR filterDesc =
    {
        0, nullptr,                       // Version, AutomationTable
        sizeof(PCPIN_DESCRIPTOR),
        SIZEOF_ARRAY(pinDesc), pinDesc,
        0, nullptr,                       // Nodes
        0, nullptr                        // Connections / Categories
    };

    *desc = &filterDesc;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveCyclicSignally::DataRangeIntersection(
    ULONG          PinId,
    PKSDATARANGE   DataRange,
    PKSDATARANGE   MatchingDataRange,
    ULONG          OutputBufferLength,
    PVOID          ResultantFormat,
    PULONG         ResultantFormatLength)
{
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);

    const ULONG required = sizeof(KSDATAFORMAT_WAVEFORMATEX);

    // Caller probes for the size first.
    if (OutputBufferLength == 0)
    {
        *ResultantFormatLength = required;
        return STATUS_BUFFER_OVERFLOW;
    }
    if (OutputBufferLength < required)
        return STATUS_BUFFER_TOO_SMALL;

    // We only support exactly gCaptureFormat — hand it back.
    RtlCopyMemory(ResultantFormat, &gCaptureFormat, required);
    *ResultantFormatLength = required;
    return STATUS_SUCCESS;
}

// ── CMiniportStreamSignally impl ─────────────────────────────────────────────

NTSTATUS
CMiniportStreamSignally::Init(
    CMiniportWaveCyclicSignally* miniport,
    ULONG pin, BOOLEAN capture, PKSDATAFORMAT format)
{
    UNREFERENCED_PARAMETER(capture);
    UNREFERENCED_PARAMETER(format);
    Miniport_ = miniport;
    Channel_  = pin;

    // Allocate the cyclic capture buffer (one second of float/48k/stereo).
    DmaBufferSize_ = SIGNALLY_SAMPLE_RATE * SIGNALLY_CHANNELS * sizeof(float);
    DmaBuffer_     = ExAllocatePool2(POOL_FLAG_NON_PAGED, DmaBufferSize_, 'fubD');
    if (DmaBuffer_ == nullptr)
    {
        DmaBufferSize_ = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(DmaBuffer_, DmaBufferSize_);
    DmaPosition_ = 0;
    return STATUS_SUCCESS;
}

CMiniportStreamSignally::~CMiniportStreamSignally()
{
    if (DmaBuffer_ != nullptr)
    {
        ExFreePoolWithTag(DmaBuffer_, 'fubD');
        DmaBuffer_ = nullptr;
    }
}

STDMETHODIMP_(NTSTATUS)
CMiniportStreamSignally::SetState(KSSTATE state)
{
    State_ = state;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportStreamSignally::GetPosition(_Out_ PULONG pos)
{
    *pos = DmaPosition_;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(ULONG)
CMiniportStreamSignally::SetNotificationFreq(ULONG interval, PULONG framing)
{
    NotificationFreq_ = interval;
    *framing = (ULONG)((double)SIGNALLY_SAMPLE_RATE * interval / 1000.0)
               * SIGNALLY_CHANNELS * sizeof(float);
    return interval;
}

STDMETHODIMP_(NTSTATUS)
CMiniportStreamSignally::NormalizePhysicalPosition(PLONGLONG pos)
{
    *pos = (*pos * SIGNALLY_SAMPLE_RATE * SIGNALLY_CHANNELS * sizeof(float))
           / 10000000LL;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportStreamSignally::SetFormat(PKSDATAFORMAT) { return STATUS_SUCCESS; }

STDMETHODIMP_(void)
CMiniportStreamSignally::Silence(PVOID buf, ULONG byteCount)
{
    RtlZeroMemory(buf, byteCount);
}

void CMiniportStreamSignally::ServiceGroup()
{
    // Pull audio the app wrote into the shared ring and stage it in the cyclic
    // capture buffer for the OS to read. Runs at each PortCls notification.
    if (State_ != KSSTATE_RUN)
        return;

    PSIGNALLY_SHARED_HEADER hdr  = g_SignallyShared.Header;
    FLOAT*                  ring = g_SignallyShared.RingBuffer;
    FLOAT*                  dma  = reinterpret_cast<FLOAT*>(DmaBuffer_);
    if (hdr == nullptr || ring == nullptr || dma == nullptr)
        return;

    const ULONG frameBytes  = SIGNALLY_CHANNELS * sizeof(float);
    const ULONG dmaFrames   = DmaBufferSize_ / frameBytes;
    if (dmaFrames == 0)
        return;

    // Frames to produce for this notification interval.
    ULONG framesThisTick = (ULONG)(((ULONGLONG) SIGNALLY_SAMPLE_RATE * NotificationFreq_) / 1000);
    if (framesThisTick == 0)
        framesThisTick = 1;

    // Snapshot positions (app increments WritePos; we own ReadPos).
    LONG wp = hdr->WritePos;
    LONG rp = hdr->ReadPos;

    for (ULONG i = 0; i < framesThisTick; ++i)
    {
        const ULONG dmaFrame = (DmaPosition_ / frameBytes) % dmaFrames;
        FLOAT* dst = dma + (SIZE_T) dmaFrame * SIGNALLY_CHANNELS;

        if (rp != wp)
        {
            const ULONG srcFrame = (ULONG)((ULONG) rp % SIGNALLY_RING_FRAMES);
            const FLOAT* src = ring + (SIZE_T) srcFrame * SIGNALLY_CHANNELS;
            for (ULONG c = 0; c < SIGNALLY_CHANNELS; ++c)
                dst[c] = src[c];
            ++rp;
        }
        else
        {
            // Ring empty (app under-running) → emit silence to keep the clock moving.
            for (ULONG c = 0; c < SIGNALLY_CHANNELS; ++c)
                dst[c] = 0.0f;
        }

        DmaPosition_ += frameBytes;
        if (DmaPosition_ >= DmaBufferSize_)
            DmaPosition_ = 0;
    }

    // Publish how much of the ring we consumed.
    hdr->ReadPos = rp;
}
