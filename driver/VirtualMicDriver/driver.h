#pragma once

// Kernel-mode includes (WDK)
#include <ntddk.h>
#include <wdm.h>
#include <portcls.h>
#include <stdunk.h>
#include <ksdebug.h>

#include "shared_memory.h"

// ── Shared context ────────────────────────────────────────────────────────────
// This is a single-instance virtual device, so the shared-memory state is held in
// one file-scope context populated by AddDevice and consumed by the capture stream.
// (A multi-instance driver would move this into the PnP device extension instead.)
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

// Driver entry points (adapter.cpp)
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     DriverUnload;

// PortCls adapter callbacks
NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject);
NTSTATUS StartDevice(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_ PRESOURCELIST ResourceList);

// Miniport factory
NTSTATUS CreateMiniport(
    _Out_ PUNKNOWN* Unknown,
    _In_  REFCLSID  ClassID,
    _In_  PUNKNOWN  UnknownOuter OPTIONAL,
    _In_  POOL_TYPE PoolType);
