/*++

Module Name:

    device.c - Device handling events for example driver.

Abstract:

   This file contains the device entry points and callbacks.
    
Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "device.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, USBDriver1CreateDevice)
#pragma alloc_text (PAGE, USBDriver1EvtDevicePrepareHardware)
#endif


NTSTATUS
USBDriver1CreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
/*++

Routine Description:

    Worker routine called to create a virtual HID device and its software resources.

Arguments:

    DeviceInit - Pointer to an opaque init structure. Memory for this
                    structure will be freed by the framework when the WdfDeviceCreate
                    succeeds. So don't access the structure after that point.

Return Value:

    NTSTATUS

--*/
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    PDEVICE_CONTEXT deviceContext;
    WDFDEVICE device;
    NTSTATUS status;

    PAGED_CODE();

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = USBDriver1EvtDevicePrepareHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

    if (NT_SUCCESS(status)) {
        deviceContext = DeviceGetContext(device);
        deviceContext->Device = device;

        // Initialize synchronization objects
        KeInitializeSpinLock(&deviceContext->CommandLock);

        // Initialize HID report buffers
        deviceContext->KeyboardReportSize = 8;
        deviceContext->MouseReportSize = 8;

        // Create a device interface so that applications can find and talk to us
        status = WdfDeviceCreateDeviceInterface(
            device,
            &GUID_DEVINTERFACE_USBDriver1,
            NULL // ReferenceString
        );

        if (NT_SUCCESS(status)) {
            // Initialize the I/O Package and any Queues
            status = USBDriver1QueueInitialize(device);
        }
    }

    return status;
}

NTSTATUS
USBDriver1EvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourceList,
    _In_ WDFCMRESLIST ResourceListTranslated
    )
/*++

Routine Description:

    In this callback, the driver initializes the virtual HID device and sets up
    shared memory communication with the user-mode application.

Arguments:

    Device - handle to a device

Return Value:

    NT status value

--*/
{
    NTSTATUS status;
    PDEVICE_CONTEXT pDeviceContext;
    UNICODE_STRING sharedMemoryName;

    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(ResourceListTranslated);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = STATUS_SUCCESS;
    pDeviceContext = DeviceGetContext(Device);

    // Initialize the HID report descriptors
    // For now, we'll set up basic keyboard and mouse reports
    // In a production driver, these would come from proper HID descriptors

    // Initialize keyboard report buffer - standard USB keyboard format
    // Byte 0: Modifier keys, Bytes 1: Reserved, Bytes 2-7: Key codes
    RtlZeroMemory(pDeviceContext->KeyboardReportBuffer, sizeof(pDeviceContext->KeyboardReportBuffer));

    // Initialize mouse report buffer - standard USB mouse format
    // Byte 0: Button state, Byte 1: X relative, Byte 2: Y relative, Byte 3: Wheel
    RtlZeroMemory(pDeviceContext->MouseReportBuffer, sizeof(pDeviceContext->MouseReportBuffer));

    // Open or create shared memory for IPC with user-mode application
    // This allows the Lua script application to send keyboard/mouse commands
    RtlInitUnicodeString(&sharedMemoryName, SHARED_MEMORY_NAME);

    // In a real implementation, you would:
    // 1. Create a named shared memory section
    // 2. Map it for kernel access
    // 3. Create synchronization objects for safe access
    // For now, we'll set up the infrastructure

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, 
                "Virtual HID device initialized successfully");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

