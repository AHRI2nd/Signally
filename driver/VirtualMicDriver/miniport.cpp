// miniport.cpp — WaveRT capture miniport for the Signally Virtual Microphone.
//
// Replaces the legacy WaveCyclic design. The OS maps the WaveRT audio buffer
// directly (zero-copy); a high-resolution EX_TIMER advances a simulated capture
// position and, at each tick, copies audio the user-mode app wrote into the
// shared-memory ring into the WaveRT buffer. Notification events drive the
// WASAPI event-driven capture path; GetReadPacket reports completed packets.
//
// Pattern derived from the WDK 'sysvad' WaveRT sample, reduced to a single
// capture pin with no volume/mute/tone/keyword/sideband features.

#include "driver.h"
#include <ksmedia.h>

// ── Capture format: 48 kHz, 32-bit float, stereo ─────────────────────────────
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
static PKSDATARANGE gCaptureAudioRanges[] = { (PKSDATARANGE)&gCaptureDataRange };

// Bridge pin uses a raw analog range (connects to the topology filter).
static KSDATARANGE gBridgeRange = { sizeof(KSDATARANGE) };
static PKSDATARANGE gBridgeRanges[] = { &gBridgeRange };

static GUID gNodeType_Audio = { STATIC_KSCATEGORY_AUDIO };

// Forward
class CMiniportWaveRTSignally;
EXT_CALLBACK SignallyTimerNotify;

// ════════════════════════════════════════════════════════════════════════════
// CMiniportWaveRTStreamSignally — one capture stream
// ════════════════════════════════════════════════════════════════════════════
class CMiniportWaveRTStreamSignally
    : public IMiniportWaveRTStreamNotification,
      public IMiniportWaveRTInputStream,
      public CUnknown
{
public:
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportWaveRTStreamSignally);
    ~CMiniportWaveRTStreamSignally();

    IMP_IMiniportWaveRTStream;
    IMP_IMiniportWaveRTStreamNotification;

    // IMiniportWaveRTInputStream
    STDMETHODIMP_(NTSTATUS) GetReadPacket(
        _Out_ ULONG* PacketNumber, _Out_ DWORD* Flags,
        _Out_ ULONG64* PerformanceCounterValue, _Out_ BOOL* MoreData);

    NTSTATUS Init(
        _In_ CMiniportWaveRTSignally* Miniport,
        _In_ PPORTWAVERTSTREAM        PortStream,
        _In_ ULONG                    Pin,
        _In_ BOOLEAN                  Capture,
        _In_ PKSDATAFORMAT            DataFormat);

    friend EXT_CALLBACK SignallyTimerNotify;

private:
    void UpdatePosition(LARGE_INTEGER qpc);
    void FillCaptureFromRing(ULONG byteDisplacement);

    CMiniportWaveRTSignally* m_pMiniport          = nullptr;
    PPORTWAVERTSTREAM        m_pPortStream         = nullptr;
    ULONG                    m_ulPin               = 0;
    BOOLEAN                  m_bCapture            = FALSE;

    BYTE*                    m_pDmaBuffer          = nullptr;
    ULONG                    m_ulDmaBufferSize     = 0;
    ULONG                    m_ulNotificationsPerBuffer = 0;
    ULONG                    m_ulNotificationIntervalMs = 0;
    ULONG                    m_ulDmaMovementRate   = 0;   // avg bytes/sec

    KSSTATE                  m_KsState             = KSSTATE_STOP;
    PEX_TIMER                m_pNotificationTimer  = nullptr;

    KSPIN_LOCK               m_PositionSpinLock;
    LIST_ENTRY               m_NotificationList;

    ULONGLONG                m_ullPlayPosition     = 0;
    ULONGLONG                m_ullWritePosition    = 0;
    ULONGLONG                m_ullLinearPosition   = 0;
    ULONGLONG                m_ullDmaTimeStamp     = 0;
    ULONGLONG                m_hnsElapsedTimeCarryForward = 0;
    ULONGLONG                m_ullLastDPCTimeStamp = 0;
    ULONGLONG                m_hnsDPCTimeCarryForward = 0;
    ULONG                    m_byteDisplacementCarryForward = 0;
    LONGLONG                 m_llPacketCounter     = 0;
    ULONG                    m_ulLastOsReadPacket  = ULONG_MAX;
    LARGE_INTEGER            m_ullPerformanceCounterFrequency = {};

    friend class CMiniportWaveRTSignally;
};
typedef CMiniportWaveRTStreamSignally* PCMiniportWaveRTStreamSignally;

// Notification list entry
typedef struct _SignallyNotifyEntry
{
    LIST_ENTRY ListEntry;
    PKEVENT    Event;
} SignallyNotifyEntry;

// ════════════════════════════════════════════════════════════════════════════
// CMiniportWaveRTSignally — the WaveRT miniport (capture filter)
// ════════════════════════════════════════════════════════════════════════════
class CMiniportWaveRTSignally
    : public IMiniportWaveRT,
      public CUnknown
{
public:
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportWaveRTSignally);
    ~CMiniportWaveRTSignally() {}

    IMP_IMiniportWaveRT;

private:
    PPORTWAVERT m_pPort = nullptr;
};
typedef CMiniportWaveRTSignally* PCMiniportWaveRTSignally;

// ── Factory ───────────────────────────────────────────────────────────────────
NTSTATUS
CreateMiniportWaveRTSignally(
    _Out_ PUNKNOWN*   Unknown,
    _In_  REFCLSID    ClassID,
    _In_  PUNKNOWN    UnknownOuter OPTIONAL,
    _In_  POOL_FLAGS  PoolFlags)
{
    UNREFERENCED_PARAMETER(ClassID);
    PAGED_CODE();

    *Unknown = new(PoolFlags, SIGNALLY_POOLTAG_WAVE)
                   CMiniportWaveRTSignally(UnknownOuter);
    if (*Unknown == nullptr)
        return STATUS_INSUFFICIENT_RESOURCES;

    (*Unknown)->AddRef();
    return STATUS_SUCCESS;
}

// ════════════════════════════════════════════════════════════════════════════
// CMiniportWaveRTSignally implementation
// ════════════════════════════════════════════════════════════════════════════

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTSignally::NonDelegatingQueryInterface(_In_ REFIID Interface, _COM_Outptr_ PVOID* Object)
{
    PAGED_CODE();
    if (IsEqualGUIDAligned(Interface, IID_IUnknown))
        *Object = static_cast<PUNKNOWN>(static_cast<PMINIPORTWAVERT>(this));
    else if (IsEqualGUIDAligned(Interface, IID_IMiniport))
        *Object = static_cast<PMINIPORT>(this);
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRT))
        *Object = static_cast<PMINIPORTWAVERT>(this);
    else
        *Object = nullptr;

    if (*Object) { static_cast<PUNKNOWN>(*Object)->AddRef(); return STATUS_SUCCESS; }
    return STATUS_INVALID_PARAMETER;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTSignally::Init(
    _In_ PUNKNOWN      UnknownAdapter,
    _In_ PRESOURCELIST ResourceList,
    _In_ PPORTWAVERT   Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    PAGED_CODE();

    m_pPort = Port;
    m_pPort->AddRef();
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTSignally::GetDeviceDescription(_Out_ PDEVICE_DESCRIPTION DeviceDescription)
{
    PAGED_CODE();
    RtlZeroMemory(DeviceDescription, sizeof(DEVICE_DESCRIPTION));
    DeviceDescription->Master            = TRUE;
    DeviceDescription->ScatterGather     = TRUE;
    DeviceDescription->Dma32BitAddresses = TRUE;
    DeviceDescription->InterfaceType     = PNPBus;
    DeviceDescription->MaximumLength     = 0xFFFFFFFF;
    return STATUS_SUCCESS;
}

// Filter descriptor: pin 0 = host capture (data OUT to OS), pin 1 = bridge (to topology).
STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTSignally::GetDescription(_Out_ PPCFILTER_DESCRIPTOR* OutDescriptor)
{
    PAGED_CODE();

    static PCPIN_DESCRIPTOR pins[] =
    {
        // Pin 0 — host capture sink (the OS reads captured audio here)
        {
            1, 1, 0, nullptr,
            {
                0, nullptr,
                0, nullptr,
                SIZEOF_ARRAY(gCaptureAudioRanges), gCaptureAudioRanges,
                KSPIN_DATAFLOW_OUT,
                KSPIN_COMMUNICATION_SINK,
                (GUID*)&KSCATEGORY_AUDIO,
                nullptr, 0
            }
        },
        // Pin 1 — bridge pin (physical connection to the topology filter)
        {
            0, 0, 0, nullptr,
            {
                0, nullptr,
                0, nullptr,
                SIZEOF_ARRAY(gBridgeRanges), gBridgeRanges,
                KSPIN_DATAFLOW_IN,
                KSPIN_COMMUNICATION_NONE,
                (GUID*)&KSNODETYPE_ANY,
                nullptr, 0
            }
        }
    };

    // Connection: bridge pin (1) → host capture pin (0). No nodes.
    static PCCONNECTION_DESCRIPTOR connections[] =
    {
        { PCFILTER_NODE, 1, PCFILTER_NODE, 0 }
    };

    static PCFILTER_DESCRIPTOR filterDesc =
    {
        0, nullptr,
        sizeof(PCPIN_DESCRIPTOR), SIZEOF_ARRAY(pins), pins,
        0, nullptr,                                   // no nodes
        SIZEOF_ARRAY(connections), connections,
        0, nullptr
    };

    *OutDescriptor = &filterDesc;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTSignally::DataRangeIntersection(
    _In_ ULONG PinId, _In_ PKSDATARANGE DataRange, _In_ PKSDATARANGE MatchingDataRange,
    _In_ ULONG OutputBufferLength, _Out_ PVOID ResultantFormat, _Out_ PULONG ResultantFormatLength)
{
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);
    PAGED_CODE();

    const ULONG required = sizeof(KSDATAFORMAT_WAVEFORMATEX);
    if (OutputBufferLength == 0) { *ResultantFormatLength = required; return STATUS_BUFFER_OVERFLOW; }
    if (OutputBufferLength < required) return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(ResultantFormat, &gCaptureFormat, required);
    *ResultantFormatLength = required;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTSignally::NewStream(
    _Out_ PMINIPORTWAVERTSTREAM* OutStream,
    _In_  PPORTWAVERTSTREAM      PortStream,
    _In_  ULONG                  Pin,
    _In_  BOOLEAN                Capture,
    _In_  PKSDATAFORMAT          DataFormat)
{
    PAGED_CODE();

    auto* stream = new(POOL_FLAG_NON_PAGED, SIGNALLY_POOLTAG_STREAM)
                       CMiniportWaveRTStreamSignally(nullptr);
    if (stream == nullptr)
        return STATUS_INSUFFICIENT_RESOURCES;
    stream->AddRef();

    NTSTATUS status = stream->Init(this, PortStream, Pin, Capture, DataFormat);
    if (!NT_SUCCESS(status)) { stream->Release(); return status; }

    *OutStream = static_cast<PMINIPORTWAVERTSTREAM>(stream);
    return STATUS_SUCCESS;
}

// ════════════════════════════════════════════════════════════════════════════
// CMiniportWaveRTStreamSignally implementation
// ════════════════════════════════════════════════════════════════════════════

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTStreamSignally::NonDelegatingQueryInterface(_In_ REFIID Interface, _COM_Outptr_ PVOID* Object)
{
    PAGED_CODE();
    if (IsEqualGUIDAligned(Interface, IID_IUnknown))
        *Object = static_cast<PUNKNOWN>(static_cast<PMINIPORTWAVERTSTREAM>(this));
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStream))
        *Object = static_cast<PMINIPORTWAVERTSTREAM>(this);
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStreamNotification))
        *Object = static_cast<PMINIPORTWAVERTSTREAMNOTIFICATION>(this);
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTInputStream))
        *Object = static_cast<PMINIPORTWAVERTINPUTSTREAM>(this);
    else
        *Object = nullptr;

    if (*Object) { static_cast<PUNKNOWN>(*Object)->AddRef(); return STATUS_SUCCESS; }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS
CMiniportWaveRTStreamSignally::Init(
    _In_ CMiniportWaveRTSignally* Miniport,
    _In_ PPORTWAVERTSTREAM        PortStream,
    _In_ ULONG                    Pin,
    _In_ BOOLEAN                  Capture,
    _In_ PKSDATAFORMAT            DataFormat)
{
    PAGED_CODE();

    m_pMiniport   = Miniport;
    m_pMiniport->AddRef();
    m_pPortStream = PortStream;
    m_ulPin       = Pin;
    m_bCapture    = Capture;
    m_KsState     = KSSTATE_STOP;

    KeInitializeSpinLock(&m_PositionSpinLock);
    InitializeListHead(&m_NotificationList);

    auto* wfx = &reinterpret_cast<PKSDATAFORMAT_WAVEFORMATEX>(DataFormat)->WaveFormatEx;
    m_ulDmaMovementRate = wfx->nAvgBytesPerSec;

    m_pNotificationTimer = ExAllocateTimer(SignallyTimerNotify, this, EX_TIMER_HIGH_RESOLUTION);
    if (m_pNotificationTimer == nullptr)
        return STATUS_INSUFFICIENT_RESOURCES;

    return STATUS_SUCCESS;
}

CMiniportWaveRTStreamSignally::~CMiniportWaveRTStreamSignally()
{
    if (m_pNotificationTimer)
    {
        ExCancelTimer(m_pNotificationTimer, nullptr);
        ExDeleteTimer(m_pNotificationTimer, TRUE, FALSE, nullptr);
        m_pNotificationTimer = nullptr;
    }
    // Drain any leftover notification registrations.
    while (!IsListEmpty(&m_NotificationList))
    {
        PLIST_ENTRY e = RemoveHeadList(&m_NotificationList);
        auto* n = CONTAINING_RECORD(e, SignallyNotifyEntry, ListEntry);
        if (n->Event) ObDereferenceObject(n->Event);
        ExFreePoolWithTag(n, SIGNALLY_POOLTAG_STREAM);
    }
    if (m_pMiniport) m_pMiniport->Release();
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTStreamSignally::AllocateAudioBuffer(
    _In_ ULONG RequestedSize, _Out_ PMDL* AudioBufferMdl, _Out_ ULONG* ActualSize,
    _Out_ ULONG* OffsetFromFirstPage, _Out_ MEMORY_CACHING_TYPE* CacheType)
{
    PAGED_CODE();

    const ULONG frameBytes = SIGNALLY_CHANNELS * sizeof(float);
    if (RequestedSize == 0 || RequestedSize < frameBytes)
        return STATUS_UNSUCCESSFUL;
    RequestedSize -= RequestedSize % frameBytes;

    PHYSICAL_ADDRESS high; high.HighPart = 0; high.LowPart = MAXULONG;
    PMDL mdl = m_pPortStream->AllocatePagesForMdl(high, RequestedSize);
    if (mdl == nullptr) return STATUS_UNSUCCESSFUL;

    m_pDmaBuffer = (BYTE*)m_pPortStream->MapAllocatedPages(mdl, MmCached);
    if (m_pDmaBuffer == nullptr) { m_pPortStream->FreePagesFromMdl(mdl); return STATUS_UNSUCCESSFUL; }
    RtlZeroMemory(m_pDmaBuffer, RequestedSize);

    m_ulDmaBufferSize          = RequestedSize;
    m_ulNotificationsPerBuffer = 0;

    *AudioBufferMdl       = mdl;
    *ActualSize           = RequestedSize;
    *OffsetFromFirstPage  = 0;
    *CacheType            = MmCached;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTStreamSignally::AllocateBufferWithNotification(
    _In_ ULONG NotificationCount, _In_ ULONG RequestedSize, _Out_ PMDL* AudioBufferMdl,
    _Out_ ULONG* ActualSize, _Out_ ULONG* OffsetFromFirstPage, _Out_ MEMORY_CACHING_TYPE* CacheType)
{
    PAGED_CODE();

    if (NotificationCount == 0 || RequestedSize == 0)
        return STATUS_INVALID_PARAMETER;
    if (RequestedSize % NotificationCount != 0)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS status = AllocateAudioBuffer(RequestedSize, AudioBufferMdl, ActualSize,
                                          OffsetFromFirstPage, CacheType);
    if (!NT_SUCCESS(status)) return status;

    m_ulNotificationsPerBuffer = NotificationCount;
    // Notification interval (ms) = bytes-per-notification / bytes-per-ms.
    ULONG bytesPerNotification = *ActualSize / NotificationCount;
    ULONG bytesPerMs = m_ulDmaMovementRate / 1000;
    m_ulNotificationIntervalMs = bytesPerMs ? (bytesPerNotification / bytesPerMs) : 10;
    if (m_ulNotificationIntervalMs == 0) m_ulNotificationIntervalMs = 1;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(VOID)
CMiniportWaveRTStreamSignally::FreeAudioBuffer(_In_opt_ PMDL Mdl, _In_ ULONG Size)
{
    UNREFERENCED_PARAMETER(Size);
    PAGED_CODE();
    if (Mdl)
    {
        if (m_pDmaBuffer) { m_pPortStream->UnmapAllocatedPages(m_pDmaBuffer, Mdl); m_pDmaBuffer = nullptr; }
        m_pPortStream->FreePagesFromMdl(Mdl);
    }
    m_ulDmaBufferSize = 0;
    m_ulNotificationsPerBuffer = 0;
}

STDMETHODIMP_(VOID)
CMiniportWaveRTStreamSignally::FreeBufferWithNotification(_In_ PMDL Mdl, _In_ ULONG Size)
{
    PAGED_CODE();
    FreeAudioBuffer(Mdl, Size);
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTStreamSignally::RegisterNotificationEvent(_In_ PKEVENT NotificationEvent)
{
    PAGED_CODE();
    auto* n = (SignallyNotifyEntry*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(SignallyNotifyEntry), SIGNALLY_POOLTAG_STREAM);
    if (n == nullptr) return STATUS_INSUFFICIENT_RESOURCES;

    ObReferenceObject(NotificationEvent);
    n->Event = NotificationEvent;

    KIRQL old;
    KeAcquireSpinLock(&m_PositionSpinLock, &old);
    InsertTailList(&m_NotificationList, &n->ListEntry);
    KeReleaseSpinLock(&m_PositionSpinLock, old);
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTStreamSignally::UnregisterNotificationEvent(_In_ PKEVENT NotificationEvent)
{
    PAGED_CODE();
    KIRQL old;
    KeAcquireSpinLock(&m_PositionSpinLock, &old);
    for (PLIST_ENTRY e = m_NotificationList.Flink; e != &m_NotificationList; e = e->Flink)
    {
        auto* n = CONTAINING_RECORD(e, SignallyNotifyEntry, ListEntry);
        if (n->Event == NotificationEvent)
        {
            RemoveEntryList(e);
            KeReleaseSpinLock(&m_PositionSpinLock, old);
            ObDereferenceObject(n->Event);
            ExFreePoolWithTag(n, SIGNALLY_POOLTAG_STREAM);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&m_PositionSpinLock, old);
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CMiniportWaveRTStreamSignally::GetClockRegister(_Out_ PKSRTAUDIO_HWREGISTER R)
{ UNREFERENCED_PARAMETER(R); PAGED_CODE(); return STATUS_NOT_IMPLEMENTED; }

STDMETHODIMP_(NTSTATUS) CMiniportWaveRTStreamSignally::GetPositionRegister(_Out_ PKSRTAUDIO_HWREGISTER R)
{ UNREFERENCED_PARAMETER(R); PAGED_CODE(); return STATUS_NOT_IMPLEMENTED; }

STDMETHODIMP_(VOID) CMiniportWaveRTStreamSignally::GetHWLatency(_Out_ PKSRTAUDIO_HWLATENCY L)
{ PAGED_CODE(); L->ChipsetDelay = 0; L->CodecDelay = 0; L->FifoSize = 0; }

STDMETHODIMP_(NTSTATUS) CMiniportWaveRTStreamSignally::SetFormat(_In_ PKSDATAFORMAT Fmt)
{ UNREFERENCED_PARAMETER(Fmt); PAGED_CODE(); return STATUS_NOT_SUPPORTED; }

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTStreamSignally::GetPosition(_Out_ PKSAUDIO_POSITION Position)
{
    KIRQL old;
    KeAcquireSpinLock(&m_PositionSpinLock, &old);
    if (m_KsState == KSSTATE_RUN)
        UpdatePosition(KeQueryPerformanceCounter(nullptr));
    Position->PlayOffset  = m_ullPlayPosition;
    Position->WriteOffset = m_ullWritePosition;
    KeReleaseSpinLock(&m_PositionSpinLock, old);
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTStreamSignally::SetState(_In_ KSSTATE State)
{
    KIRQL old;
    switch (State)
    {
        case KSSTATE_STOP:
            KeAcquireSpinLock(&m_PositionSpinLock, &old);
            m_llPacketCounter = 0; m_ullPlayPosition = 0; m_ullWritePosition = 0;
            m_ullLinearPosition = 0; m_ulLastOsReadPacket = ULONG_MAX;
            KeReleaseSpinLock(&m_PositionSpinLock, old);
            break;

        case KSSTATE_ACQUIRE:
        case KSSTATE_PAUSE:
            if (m_KsState == KSSTATE_RUN && m_ulNotificationIntervalMs > 0)
            {
                ExCancelTimer(m_pNotificationTimer, nullptr);
                KeFlushQueuedDpcs();
            }
            break;

        case KSSTATE_RUN:
        {
            LARGE_INTEGER perf = KeQueryPerformanceCounter(&m_ullPerformanceCounterFrequency);
            m_ullLastDPCTimeStamp = m_ullDmaTimeStamp =
                KSCONVERT_PERFORMANCE_TIME(m_ullPerformanceCounterFrequency.QuadPart, perf);
            if (m_ulNotificationIntervalMs > 0)
                ExSetTimer(m_pNotificationTimer, -1 * 10000LL, 10000LL, nullptr); // 1 ms tick
            break;
        }
    }
    m_KsState = State;
    return STATUS_SUCCESS;
}

// Copy audio the app wrote into the shared ring into the WaveRT capture buffer.
void CMiniportWaveRTStreamSignally::FillCaptureFromRing(ULONG byteDisplacement)
{
    PSIGNALLY_SHARED_HEADER hdr  = g_SignallyShared.Header;
    FLOAT*                  ring = g_SignallyShared.RingBuffer;
    if (m_pDmaBuffer == nullptr || m_ulDmaBufferSize == 0)
        return;

    const ULONG frameBytes = SIGNALLY_CHANNELS * sizeof(float);
    ULONG bufferOffset = (ULONG)(m_ullLinearPosition % m_ulDmaBufferSize);

    LONG wp = hdr ? hdr->WritePos : 0;
    LONG rp = hdr ? hdr->ReadPos  : 0;

    while (byteDisplacement >= frameBytes)
    {
        ULONG run = min(byteDisplacement, m_ulDmaBufferSize - bufferOffset);
        ULONG frames = run / frameBytes;
        FLOAT* dst = reinterpret_cast<FLOAT*>(m_pDmaBuffer + bufferOffset);

        for (ULONG f = 0; f < frames; ++f)
        {
            if (hdr && ring && rp != wp)
            {
                const ULONG srcFrame = (ULONG)((ULONG)rp % SIGNALLY_RING_FRAMES);
                const FLOAT* src = ring + (SIZE_T)srcFrame * SIGNALLY_CHANNELS;
                for (ULONG c = 0; c < SIGNALLY_CHANNELS; ++c) dst[c] = src[c];
                ++rp;
            }
            else
            {
                for (ULONG c = 0; c < SIGNALLY_CHANNELS; ++c) dst[c] = 0.0f; // underrun → silence
            }
            dst += SIGNALLY_CHANNELS;
        }
        bufferOffset = (bufferOffset + frames * frameBytes) % m_ulDmaBufferSize;
        byteDisplacement -= frames * frameBytes;
    }

    if (hdr) hdr->ReadPos = rp;  // publish how much of the ring we consumed
}

void CMiniportWaveRTStreamSignally::UpdatePosition(LARGE_INTEGER qpc)
{
    LONGLONG hns = KSCONVERT_PERFORMANCE_TIME(m_ullPerformanceCounterFrequency.QuadPart, qpc);
    ULONG msElapsed = (ULONG)(hns - m_ullDmaTimeStamp + m_hnsElapsedTimeCarryForward) / 10000;
    m_hnsElapsedTimeCarryForward = (hns - m_ullDmaTimeStamp + m_hnsElapsedTimeCarryForward) % 10000;

    ULONG byteDisp = (m_ulDmaMovementRate * msElapsed + m_byteDisplacementCarryForward) / 1000;
    m_byteDisplacementCarryForward = (m_ulDmaMovementRate * msElapsed + m_byteDisplacementCarryForward) % 1000;

    if (m_bCapture)
        FillCaptureFromRing(byteDisp);

    m_ullPlayPosition = m_ullWritePosition = (m_ullWritePosition + byteDisp) % m_ulDmaBufferSize;
    m_ullLinearPosition += byteDisp;
    m_ullDmaTimeStamp = hns;
}

STDMETHODIMP_(NTSTATUS)
CMiniportWaveRTStreamSignally::GetReadPacket(
    _Out_ ULONG* PacketNumber, _Out_ DWORD* Flags,
    _Out_ ULONG64* PerformanceCounterValue, _Out_ BOOL* MoreData)
{
    if (m_ulNotificationsPerBuffer == 0) return STATUS_NOT_SUPPORTED;
    if (m_KsState < KSSTATE_PAUSE)       return STATUS_INVALID_DEVICE_STATE;

    *Flags = 0;

    KIRQL old;
    KeAcquireSpinLock(&m_PositionSpinLock, &old);
    LONGLONG packetCounter = m_llPacketCounter;
    KeReleaseSpinLock(&m_PositionSpinLock, old);

    ULONG available = LODWORD(packetCounter - 1);
    if (available == m_ulLastOsReadPacket)
        return STATUS_DEVICE_NOT_READY;

    *PacketNumber            = available;
    *PerformanceCounterValue = (ULONG64)KeQueryPerformanceCounter(nullptr).QuadPart;
    *MoreData                = FALSE;
    m_ulLastOsReadPacket     = available;
    return STATUS_SUCCESS;
}

// ── High-resolution timer callback (DISPATCH_LEVEL) ──────────────────────────
void SignallyTimerNotify(_In_ PEX_TIMER Timer, _In_opt_ PVOID DeferredContext)
{
    UNREFERENCED_PARAMETER(Timer);
    auto* s = reinterpret_cast<CMiniportWaveRTStreamSignally*>(DeferredContext);
    if (s == nullptr) return;

    KIRQL old;
    KeAcquireSpinLock(&s->m_PositionSpinLock, &old);

    LARGE_INTEGER freq;
    LARGE_INTEGER qpc = KeQueryPerformanceCounter(&freq);
    LONGLONG hns = KSCONVERT_PERFORMANCE_TIME(s->m_ullPerformanceCounterFrequency.QuadPart, qpc);

    ULONG msElapsed = (ULONG)(hns - s->m_ullLastDPCTimeStamp + s->m_hnsDPCTimeCarryForward) / 10000;
    if (msElapsed < s->m_ulNotificationIntervalMs)
        goto End;

    s->m_hnsDPCTimeCarryForward = hns - s->m_ullLastDPCTimeStamp + s->m_hnsDPCTimeCarryForward
                                  - (s->m_ulNotificationIntervalMs * 10000LL);
    s->m_ullLastDPCTimeStamp = hns;

    s->UpdatePosition(qpc);
    s->m_llPacketCounter++;

    if (s->m_KsState == KSSTATE_RUN && !IsListEmpty(&s->m_NotificationList))
    {
        for (PLIST_ENTRY e = s->m_NotificationList.Flink; e != &s->m_NotificationList; e = e->Flink)
        {
            auto* n = CONTAINING_RECORD(e, SignallyNotifyEntry, ListEntry);
            KeSetEvent(n->Event, 0, FALSE);
        }
    }

End:
    KeReleaseSpinLock(&s->m_PositionSpinLock, old);
}
