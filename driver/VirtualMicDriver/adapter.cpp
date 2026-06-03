// adapter.cpp — PortCls adapter for Signally Virtual Microphone Driver
//
// Build: WDK + Visual Studio 2022 (driver project, not app project)
// Link:  portcls.lib, ks.lib, ksguid.lib

#include "driver.h"

// Device interface GUID — generated specifically for Signally
// {A9B3F210-1234-5678-ABCD-EF0123456789}
DEFINE_GUID(SIGNALLY_INTERFACE_GUID,
    0xa9b3f210, 0x1234, 0x5678,
    0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89);

// Single-instance shared-memory context (declared extern in driver.h).
SIGNALLY_SHARED g_SignallyShared = { 0 };

// ── DriverEntry ───────────────────────────────────────────────────────────────

extern "C"
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    // Let PortCls handle AddDevice, IRP dispatching, and PnP
    status = PcInitializeAdapterDriver(DriverObject, RegistryPath, AddDevice);
    if (!NT_SUCCESS(status))
        return status;

    DriverObject->DriverUnload = DriverUnload;
    return STATUS_SUCCESS;
}

extern "C"
VOID
DriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    // Release the shared section + event mapped in AddDevice.
    if (g_SignallyShared.Header != nullptr)
        MmUnmapViewInSystemSpace(g_SignallyShared.Header);
    if (g_SignallyShared.SectionObject != nullptr)
        ObDereferenceObject(g_SignallyShared.SectionObject);
    if (g_SignallyShared.SectionHandle != nullptr)
        ZwClose(g_SignallyShared.SectionHandle);
    if (g_SignallyShared.WriteEvent != nullptr)
        ObDereferenceObject(g_SignallyShared.WriteEvent);

    RtlZeroMemory(&g_SignallyShared, sizeof(g_SignallyShared));
}

// ── AddDevice ─────────────────────────────────────────────────────────────────

NTSTATUS
AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    NTSTATUS status;

    // Create the shared memory section that user-mode app will open
    UNICODE_STRING sectionName = RTL_CONSTANT_STRING(SIGNALLY_SHARED_MEM_NAME);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &sectionName,
                                OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                nullptr, nullptr);

    LARGE_INTEGER maxSize;
    maxSize.QuadPart = SIGNALLY_SHARED_MEM_SIZE;

    HANDLE sectionHandle = nullptr;
    PVOID  sectionObj    = nullptr;
    status = ZwCreateSection(&sectionHandle,
                              SECTION_ALL_ACCESS,
                              &oa,
                              &maxSize,
                              PAGE_READWRITE,
                              SEC_COMMIT,
                              nullptr);
    if (!NT_SUCCESS(status) && status != STATUS_OBJECT_NAME_COLLISION)
        return status;

    // Map section into system space so driver can access ring buffer
    PVOID  mappedBase = nullptr;
    SIZE_T viewSize   = SIGNALLY_SHARED_MEM_SIZE;
    if (sectionHandle)
    {
        status = ObReferenceObjectByHandle(sectionHandle, SECTION_ALL_ACCESS,
                                           nullptr, KernelMode, &sectionObj, nullptr);
        if (NT_SUCCESS(status))
        {
            status = MmMapViewInSystemSpace(sectionObj, &mappedBase, &viewSize);
        }
    }

    // Publish the shared-memory layout for the capture stream to consume.
    if (NT_SUCCESS(status) && mappedBase != nullptr)
    {
        g_SignallyShared.SectionHandle = sectionHandle;
        g_SignallyShared.SectionObject = sectionObj;
        g_SignallyShared.Header        = reinterpret_cast<PSIGNALLY_SHARED_HEADER>(mappedBase);
        g_SignallyShared.RingBuffer    = reinterpret_cast<FLOAT*>(g_SignallyShared.Header + 1);

        // Initialise the header so the ring is well-defined before the app attaches.
        g_SignallyShared.Header->WritePos   = 0;
        g_SignallyShared.Header->ReadPos    = 0;
        g_SignallyShared.Header->SampleRate = SIGNALLY_SAMPLE_RATE;
        g_SignallyShared.Header->Channels   = SIGNALLY_CHANNELS;
        RtlZeroMemory((PVOID)g_SignallyShared.Header->Reserved,
                      sizeof(g_SignallyShared.Header->Reserved));
    }

    // Create the write-notification event
    UNICODE_STRING evtName = RTL_CONSTANT_STRING(SIGNALLY_WRITE_EVENT_NAME);
    OBJECT_ATTRIBUTES evtOa;
    InitializeObjectAttributes(&evtOa, &evtName,
                                OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                nullptr, nullptr);
    HANDLE evtHandle = nullptr;
    PKEVENT writeEvent = nullptr;
    status = ZwCreateEvent(&evtHandle, EVENT_ALL_ACCESS, &evtOa,
                            NotificationEvent, FALSE);
    if (NT_SUCCESS(status))
    {
        ObReferenceObjectByHandle(evtHandle, EVENT_ALL_ACCESS, *ExEventObjectType,
                                   KernelMode, reinterpret_cast<PVOID*>(&writeEvent),
                                   nullptr);
        ZwClose(evtHandle);
        g_SignallyShared.WriteEvent = writeEvent;
    }

    // Let PortCls create the FDO and call our StartDevice.
    // MaxObjects = 2 (Wave + Topology subdevices).
    status = PcAddAdapterDevice(DriverObject, PhysicalDeviceObject,
                                 PCPFNSTARTDEVICE(StartDevice),
                                 2, 0);
    return status;
}

// ── Subdevice install helper ──────────────────────────────────────────────────
static NTSTATUS
InstallSubdevice(
    _In_  PDEVICE_OBJECT DeviceObject,
    _In_  PIRP           Irp,
    _In_  PWSTR          Name,
    _In_  REFGUID        PortClassId,
    _In_  NTSTATUS     (*CreateMiniport)(PUNKNOWN*, REFCLSID, PUNKNOWN, POOL_FLAGS),
    _Out_ PUNKNOWN*      OutPort)
{
    *OutPort = nullptr;

    PPORT port = nullptr;
    NTSTATUS status = PcNewPort(&port, PortClassId);
    if (!NT_SUCCESS(status)) return status;

    PUNKNOWN miniport = nullptr;
    status = CreateMiniport(&miniport, GUID_NULL, nullptr, POOL_FLAG_NON_PAGED);
    if (!NT_SUCCESS(status)) { port->Release(); return status; }

    status = port->Init(DeviceObject, Irp, miniport, nullptr, nullptr);
    miniport->Release();
    if (!NT_SUCCESS(status)) { port->Release(); return status; }

    status = PcRegisterSubdevice(DeviceObject, Name, port);
    if (!NT_SUCCESS(status)) { port->Release(); return status; }

    // Hand the port (as IUnknown) back so the caller can wire the physical
    // connection; caller owns this reference.
    port->QueryInterface(IID_IUnknown, (PVOID*)OutPort);
    port->Release();
    return STATUS_SUCCESS;
}

// Builds the Topology + WaveRT subdevices and the physical connection between them.
NTSTATUS StartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PRESOURCELIST  ResourceList)
{
    UNREFERENCED_PARAMETER(ResourceList);

    PUNKNOWN topoPort = nullptr;
    PUNKNOWN wavePort = nullptr;

    NTSTATUS status = InstallSubdevice(DeviceObject, Irp, SIGNALLY_TOPO_NAME,
                                       CLSID_PortTopology, CreateMiniportTopologySignally,
                                       &topoPort);
    if (!NT_SUCCESS(status)) goto Exit;

    status = InstallSubdevice(DeviceObject, Irp, SIGNALLY_WAVE_NAME,
                              CLSID_PortWaveRT, CreateMiniportWaveRTSignally,
                              &wavePort);
    if (!NT_SUCCESS(status)) goto Exit;

    // Physical connection: topology bridge pin (0, OUT) → wave bridge pin (1, IN).
    status = PcRegisterPhysicalConnection(DeviceObject,
                                          topoPort, 0,
                                          wavePort, 1);

Exit:
    if (topoPort) topoPort->Release();
    if (wavePort) wavePort->Release();
    return status;
}
