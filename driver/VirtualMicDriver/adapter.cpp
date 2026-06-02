// adapter.cpp — PortCls adapter for MicTrans Virtual Microphone Driver
//
// Build: WDK + Visual Studio 2022 (driver project, not app project)
// Link:  portcls.lib, ks.lib, ksguid.lib

#include "driver.h"

// Device interface GUID — generated specifically for MicTrans
// {A9B3F210-1234-5678-ABCD-EF0123456789}
DEFINE_GUID(MICTRANS_INTERFACE_GUID,
    0xa9b3f210, 0x1234, 0x5678,
    0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89);

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
}

// ── AddDevice ─────────────────────────────────────────────────────────────────

NTSTATUS
AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    NTSTATUS status;

    // Create the shared memory section that user-mode app will open
    UNICODE_STRING sectionName = RTL_CONSTANT_STRING(MICTRANS_SHARED_MEM_NAME);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &sectionName,
                                OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                nullptr, nullptr);

    LARGE_INTEGER maxSize;
    maxSize.QuadPart = MICTRANS_SHARED_MEM_SIZE;

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
    SIZE_T viewSize   = MICTRANS_SHARED_MEM_SIZE;
    if (sectionHandle)
    {
        status = ObReferenceObjectByHandle(sectionHandle, SECTION_ALL_ACCESS,
                                           nullptr, KernelMode, &sectionObj, nullptr);
        if (NT_SUCCESS(status))
        {
            status = MmMapViewInSystemSpace(sectionObj, &mappedBase, &viewSize);
        }
    }

    // Create the write-notification event
    UNICODE_STRING evtName = RTL_CONSTANT_STRING(MICTRANS_WRITE_EVENT_NAME);
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
    }

    // Let PortCls create the FDO and call our StartDevice
    status = PcAddAdapterDevice(DriverObject, PhysicalDeviceObject,
                                 PCPFNSTARTDEVICE(StartDevice),
                                 1, 0);
    return status;
}

// Forward declaration (miniport.cpp provides CreateMiniportTopology/Wavecyclic)
NTSTATUS StartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PRESOURCELIST  ResourceList)
{
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(ResourceList);

    // Register the miniport with PortCls
    PPORT  port     = nullptr;
    PUNKNOWN miniport = nullptr;

    NTSTATUS status = PcNewPort(&port, CLSID_PortWaveCyclic);
    if (!NT_SUCCESS(status)) return status;

    status = CreateMiniport(&miniport, CLSID_MiniportDriverWaveCyclic,
                             nullptr, NonPagedPoolNx);
    if (!NT_SUCCESS(status)) { port->Release(); return status; }

    status = port->Init(DeviceObject, Irp, miniport, nullptr, nullptr);
    miniport->Release();

    if (!NT_SUCCESS(status)) { port->Release(); return status; }

    status = PcRegisterSubdevice(DeviceObject, L"Wave", port);
    port->Release();
    return status;
}
