/*++

Module Name:

    device.h

Abstract:

    This file contains the device definitions.

Environment:

    Kernel-mode Driver Framework

--*/

#ifndef _DEVICE_H_
#define _DEVICE_H_

#include "public.h"
#include "shared.h"

EXTERN_C_START

//
// The device context for the virtual HID keyboard/mouse device
// This performs the same function as a WDM device extension
//
typedef struct _DEVICE_CONTEXT
{
    WDFDEVICE Device;                    // Handle to the WDF device

    // Virtual HID device information
    PVOID HidDescriptor;                 // Pointer to HID descriptor
    PUCHAR HidReportDescriptor;          // Pointer to HID report descriptor
    ULONG HidReportDescriptorLength;     // Length of HID report descriptor

    // Shared memory for IPC with user-mode application
    HANDLE SharedMemoryHandle;           // Handle to shared memory
    PSHARED_BUFFER SharedBuffer;         // Mapped shared buffer pointer
    SIZE_T SharedBufferSize;             // Size of shared buffer

    // Keyboard HID report buffer
    UCHAR KeyboardReportBuffer[8];       // Standard USB keyboard report (8 bytes)
    ULONG KeyboardReportSize;

    // Mouse HID report buffer  
    UCHAR MouseReportBuffer[8];          // Standard USB mouse report (8 bytes)
    ULONG MouseReportSize;

    // Synchronization
    KSPIN_LOCK CommandLock;              // Spinlock for command processing
    HANDLE CommandEvent;                 // Event signaled when command available

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

//
// This macro will generate an inline function called DeviceGetContext
// which will be used to get a pointer to the device context memory
// in a type safe manner.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

//
// Function to initialize the device's queues and callbacks
//
NTSTATUS
USBDriver1CreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    );

//
// Function to select the device's USB configuration and get a WDFUSBDEVICE
// handle
//
EVT_WDF_DEVICE_PREPARE_HARDWARE USBDriver1EvtDevicePrepareHardware;

EXTERN_C_END

#endif // _DEVICE_H_
