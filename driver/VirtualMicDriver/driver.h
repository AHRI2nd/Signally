#pragma once

// Enable the IMP_* method-declaration macros provided by portcls.h
// (used by the WaveRT / Topology miniport class declarations).
#define PC_IMPLEMENTATION 1

// Kernel-mode includes (WDK)
#include <ntddk.h>
#include <wdm.h>
#include <portcls.h>
#include <stdunk.h>
#include <ksdebug.h>

#include "shared_memory.h"

// ── Pool tags ───────────────────────────────────────────────────────────────
#define SIGNALLY_POOLTAG        'lngS'   // generic
#define SIGNALLY_POOLTAG_WAVE   'wngS'
#define SIGNALLY_POOLTAG_TOPO   'tngS'
#define SIGNALLY_POOLTAG_STREAM 'sngS'

// ── Subdevice names (must match the INF interface references) ─────────────────
#define SIGNALLY_WAVE_NAME      L"Wave"
#define SIGNALLY_TOPO_NAME      L"Topology"

// ── Shared context ────────────────────────────────────────────────────────────
// Single-instance virtual device: the shared-memory state lives in one
// file-scope context populated by AddDevice and consumed by the capture stream.
typedef struct _SIGNALLY_SHARED
{
    HANDLE                  SectionHandle;   // kernel handle to the named section
    PVOID                   SectionObject;   // referenced section object
    PSIGNALLY_SHARED_HEADER Header;          // mapped kernel VA (ring follows header)
    FLOAT*                  RingBuffer;      // = (FLOAT*)(Header + 1), interleaved
    PKEVENT                 WriteEvent;      // signalled by the user-mode app
} SIGNALLY_SHARED, *PSIGNALLY_SHARED;

extern SIGNALLY_SHARED g_SignallyShared;

// The app advertises an allowed consumer PID in Header->Reserved[0]:
//   0        → any process may open the capture pin (default; Discord etc. work)
//   non-zero → only that PID may open the pin; others are denied (max isolation)
#define SIGNALLY_ALLOWED_PID_INDEX 0

// ── Driver entry points (adapter.cpp) ─────────────────────────────────────────
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     DriverUnload;

NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject);
NTSTATUS StartDevice(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_ PRESOURCELIST ResourceList);

// ── Miniport factories ────────────────────────────────────────────────────────
// WaveRT capture miniport (miniport.cpp)
NTSTATUS CreateMiniportWaveRTSignally(
    _Out_ PUNKNOWN* Unknown,
    _In_  REFCLSID  ClassID,
    _In_  PUNKNOWN  UnknownOuter OPTIONAL,
    _In_  POOL_FLAGS PoolFlags);

// Topology miniport (topology.cpp) — exposes the microphone endpoint
NTSTATUS CreateMiniportTopologySignally(
    _Out_ PUNKNOWN* Unknown,
    _In_  REFCLSID  ClassID,
    _In_  PUNKNOWN  UnknownOuter OPTIONAL,
    _In_  POOL_FLAGS PoolFlags);
