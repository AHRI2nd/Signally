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
        MICTRANS_CHANNELS,
        MICTRANS_SAMPLE_RATE,
        MICTRANS_SAMPLE_RATE * MICTRANS_CHANNELS * sizeof(float),
        MICTRANS_CHANNELS * sizeof(float),
        32,
        0
    }
};

// ── CMiniportWaveCyclicMicTrans ───────────────────────────────────────────────
class CMiniportWaveCyclicMicTrans
    : public IMiniportWaveCyclic,
      public CUnknown
{
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportWaveCyclicMicTrans);

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

// ── CMiniportStreamMicTrans ───────────────────────────────────────────────────
class CMiniportStreamMicTrans
    : public IMiniportWaveCyclicStream,
      public CUnknown
{
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportStreamMicTrans);

public:
    STDMETHODIMP_(NTSTATUS) Init(
        CMiniportWaveCyclicMicTrans*, ULONG, BOOLEAN, PKSDATAFORMAT) ;

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
    CMiniportWaveCyclicMicTrans* Miniport_ = nullptr;
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

    *Unknown = new(PoolType, 'cmiM') CMiniportWaveCyclicMicTrans(UnknownOuter);
    if (!*Unknown) return STATUS_INSUFFICIENT_RESOURCES;
    (*Unknown)->AddRef();
    return STATUS_SUCCESS;
}

// ── CMiniportWaveCyclicMicTrans impl ─────────────────────────────────────────

STDMETHODIMP_(NTSTATUS)
CMiniportWaveCyclicMicTrans::Init(
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
CMiniportWaveCyclicMicTrans::NewStream(
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

    auto* stream = new(PoolType, 'rtSM') CMiniportStreamMicTrans(OuterUnknown);
    if (!stream) return STATUS_INSUFFICIENT_RESOURCES;

    NTSTATUS status = stream->Init(this, Pin, Capture, DataFormat);
    if (!NT_SUCCESS(status)) { stream->Release(); return status; }

    // Allocate DMA buffer (one second of audio)
    ULONG bufSize = MICTRANS_SAMPLE_RATE * MICTRANS_CHANNELS * sizeof(float);
    *DmaChannel   = nullptr; // software DMA

    // Create service group for notifications
    status = PcNewServiceGroup(ServiceGroup, nullptr);
    if (!NT_SUCCESS(status)) { stream->Release(); return status; }

    *Stream = stream;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveCyclicMicTrans::GetDescription(_Out_ PPCFILTER_DESCRIPTOR* desc)
{
    // Minimal filter descriptor — one capture pin
    static PCPIN_DESCRIPTOR pinDesc[] = {
        { 1, 1, 0, nullptr, { /* bridge pin */ } }
    };
    static PCFILTER_DESCRIPTOR filterDesc = {
        0, nullptr, sizeof(PCPIN_DESCRIPTOR),
        SIZEOF_ARRAY(pinDesc), pinDesc,
        0, nullptr, 0, nullptr
    };
    *desc = &filterDesc;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveCyclicMicTrans::DataRangeIntersection(
    ULONG, PKSDATARANGE, PKSDATARANGE, ULONG, PVOID, PULONG) { return STATUS_NOT_IMPLEMENTED; }

// ── CMiniportStreamMicTrans impl ─────────────────────────────────────────────

NTSTATUS
CMiniportStreamMicTrans::Init(
    CMiniportWaveCyclicMicTrans* miniport,
    ULONG pin, BOOLEAN capture, PKSDATAFORMAT format)
{
    UNREFERENCED_PARAMETER(capture);
    UNREFERENCED_PARAMETER(format);
    Miniport_ = miniport;
    Channel_  = pin;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportStreamMicTrans::SetState(KSSTATE state)
{
    State_ = state;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportStreamMicTrans::GetPosition(_Out_ PULONG pos)
{
    *pos = DmaPosition_;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(ULONG)
CMiniportStreamMicTrans::SetNotificationFreq(ULONG interval, PULONG framing)
{
    NotificationFreq_ = interval;
    *framing = (ULONG)((double)MICTRANS_SAMPLE_RATE * interval / 1000.0)
               * MICTRANS_CHANNELS * sizeof(float);
    return interval;
}

STDMETHODIMP_(NTSTATUS)
CMiniportStreamMicTrans::NormalizePhysicalPosition(PLONGLONG pos)
{
    *pos = (*pos * MICTRANS_SAMPLE_RATE * MICTRANS_CHANNELS * sizeof(float))
           / 10000000LL;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportStreamMicTrans::SetFormat(PKSDATAFORMAT) { return STATUS_SUCCESS; }

STDMETHODIMP_(void)
CMiniportStreamMicTrans::Silence(PVOID buf, ULONG byteCount)
{
    RtlZeroMemory(buf, byteCount);
}

void CMiniportStreamMicTrans::ServiceGroup()
{
    // Copy from shared ring buffer to DMA buffer
    // (DmaBuffer_ and SharedHeader are obtained via device extension in a real impl)
    // Placeholder: in production, obtain via IoGetDeviceObjectPointer + DevExt
}
