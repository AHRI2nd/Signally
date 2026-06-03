// topology.cpp — Topology miniport for the Signally Virtual Microphone.
//
// Exposes the microphone endpoint. Data flows: physical mic pin (IN) →
// bridge pin (OUT) → (physical connection) → WaveRT filter's bridge pin.
// Minimal: no volume/mute/peak nodes.

#include "driver.h"
#include <ksmedia.h>

static GUID gTopoMicCategory    = { STATIC_KSNODETYPE_MICROPHONE };

// Bridge data range (raw/analog) shared by both pins.
static KSDATARANGE gTopoBridgeRange = { sizeof(KSDATARANGE) };
static PKSDATARANGE gTopoBridgeRanges[] = { &gTopoBridgeRange };

// ════════════════════════════════════════════════════════════════════════════
class CMiniportTopologySignally
    : public IMiniportTopology,
      public CUnknown
{
public:
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportTopologySignally);
    ~CMiniportTopologySignally() { if (m_pPort) m_pPort->Release(); }

    IMP_IMiniportTopology;

private:
    PPORTTOPOLOGY m_pPort = nullptr;
};

// ── Factory ───────────────────────────────────────────────────────────────────
NTSTATUS
CreateMiniportTopologySignally(
    _Out_ PUNKNOWN*   Unknown,
    _In_  REFCLSID    ClassID,
    _In_  PUNKNOWN    UnknownOuter OPTIONAL,
    _In_  POOL_FLAGS  PoolFlags)
{
    UNREFERENCED_PARAMETER(ClassID);
    PAGED_CODE();

    *Unknown = new(PoolFlags, SIGNALLY_POOLTAG_TOPO)
                   CMiniportTopologySignally(UnknownOuter);
    if (*Unknown == nullptr)
        return STATUS_INSUFFICIENT_RESOURCES;

    (*Unknown)->AddRef();
    return STATUS_SUCCESS;
}

// ════════════════════════════════════════════════════════════════════════════

STDMETHODIMP_(NTSTATUS)
CMiniportTopologySignally::NonDelegatingQueryInterface(_In_ REFIID Interface, _COM_Outptr_ PVOID* Object)
{
    PAGED_CODE();
    if (IsEqualGUIDAligned(Interface, IID_IUnknown))
        *Object = static_cast<PUNKNOWN>(static_cast<PMINIPORTTOPOLOGY>(this));
    else if (IsEqualGUIDAligned(Interface, IID_IMiniport))
        *Object = static_cast<PMINIPORT>(this);
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportTopology))
        *Object = static_cast<PMINIPORTTOPOLOGY>(this);
    else
        *Object = nullptr;

    if (*Object) { static_cast<PUNKNOWN>(*Object)->AddRef(); return STATUS_SUCCESS; }
    return STATUS_INVALID_PARAMETER;
}

STDMETHODIMP_(NTSTATUS)
CMiniportTopologySignally::Init(
    _In_ PUNKNOWN UnknownAdapter, _In_ PRESOURCELIST ResourceList, _In_ PPORTTOPOLOGY Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    PAGED_CODE();
    m_pPort = Port;
    m_pPort->AddRef();
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CMiniportTopologySignally::GetDescription(_Out_ PPCFILTER_DESCRIPTOR* OutDescriptor)
{
    PAGED_CODE();

    static PCPIN_DESCRIPTOR pins[] =
    {
        // Pin 0 — bridge pin to the WaveRT filter (data OUT of topology)
        {
            0, 0, 0, nullptr,
            {
                0, nullptr,
                0, nullptr,
                SIZEOF_ARRAY(gTopoBridgeRanges), gTopoBridgeRanges,
                KSPIN_DATAFLOW_OUT,
                KSPIN_COMMUNICATION_NONE,
                (GUID*)&KSNODETYPE_ANY,
                nullptr, 0
            }
        },
        // Pin 1 — physical microphone input (data IN to topology)
        {
            0, 0, 0, nullptr,
            {
                0, nullptr,
                0, nullptr,
                SIZEOF_ARRAY(gTopoBridgeRanges), gTopoBridgeRanges,
                KSPIN_DATAFLOW_IN,
                KSPIN_COMMUNICATION_NONE,
                &gTopoMicCategory,
                nullptr, 0
            }
        }
    };

    // Connection: physical mic pin (1) → bridge pin (0). No nodes.
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
CMiniportTopologySignally::DataRangeIntersection(
    _In_ ULONG PinId, _In_ PKSDATARANGE DataRange, _In_ PKSDATARANGE MatchingDataRange,
    _In_ ULONG OutputBufferLength, _Out_ PVOID ResultantFormat, _Out_ PULONG ResultantFormatLength)
{
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ResultantFormat);
    UNREFERENCED_PARAMETER(ResultantFormatLength);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;   // bridge pins carry no data format
}
