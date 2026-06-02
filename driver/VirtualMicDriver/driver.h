#pragma once

// Kernel-mode includes (WDK)
#include <ntddk.h>
#include <wdm.h>
#include <portcls.h>
#include <stdunk.h>
#include <ksdebug.h>

#include "shared_memory.h"

// ── Device extension ──────────────────────────────────────────────────────────
typedef struct _DEVICE_EXTENSION
{
    // Shared memory
    PMDL                 SharedMdl;        // MDL locking the shared section
    PMICTRANS_SHARED_HEADER SharedHeader; // mapped kernel VA
    FLOAT*               RingBuffer;      // = (FLOAT*)(SharedHeader + 1)

    // Synchronisation
    PKEVENT              WriteEvent;      // signalled by user-mode app

    // Section object for shared memory
    HANDLE               SectionHandle;
    PVOID                SectionObject;

} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

// Driver entry points (adapter.cpp)
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     DriverUnload;

// PortCls adapter callback
NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject);

// Miniport factory
NTSTATUS CreateMiniport(
    _Out_ PUNKNOWN* Unknown,
    _In_  REFCLSID  ClassID,
    _In_  PUNKNOWN  UnknownOuter OPTIONAL,
    _In_  POOL_TYPE PoolType);
