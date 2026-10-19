/*++

Module Name:

    queue.c

Abstract:

    This file contains the queue entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "device.h"
#include "public.h"
#include "hidreport.h"
#include "queue.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, USBDriver1QueueInitialize)
#endif

NTSTATUS
USBDriver1QueueInitialize(
    _In_ WDFDEVICE Device
    )
/*++

Routine Description:


     The I/O dispatch callbacks for the frameworks device object
     are configured in this function.

     A single default I/O Queue is configured for parallel request
     processing, and a driver context memory allocation is created
     to hold our structure QUEUE_CONTEXT.

Arguments:

    Device - Handle to a framework device object.

Return Value:

    VOID

--*/
{
    WDFQUEUE queue;
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG    queueConfig;

    PAGED_CODE();
    
    //
    // Configure a default queue so that requests that are not
    // configure-fowarded using WdfDeviceConfigureRequestDispatching to goto
    // other queues get dispatched here.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
         &queueConfig,
        WdfIoQueueDispatchParallel
        );

    queueConfig.EvtIoDeviceControl = USBDriver1EvtIoDeviceControl;
    queueConfig.EvtIoStop = USBDriver1EvtIoStop;

    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &queue
                 );

    if( !NT_SUCCESS(status) ) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        return status;
    }

    return status;
}

VOID
USBDriver1EvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
/*++

Routine Description:

    This event is invoked when the framework receives IRP_MJ_DEVICE_CONTROL request.
    Handles keyboard, relative mouse, and absolute mouse input commands from user-mode.

Arguments:

    Queue -  Handle to the framework queue object that is associated with the I/O request.
    Request - Handle to a framework request object.
    OutputBufferLength - Size of the output buffer in bytes
    InputBufferLength - Size of the input buffer in bytes
    IoControlCode - I/O control code.

Return Value:

    VOID

--*/
{
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    PDEVICE_CONTEXT deviceContext;
    WDFDEVICE device;
    PVOID inputBuffer = NULL;
    PVOID outputBuffer = NULL;
    size_t bytesTransferred = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "%!FUNC! Queue 0x%p, Request 0x%p IOCTL 0x%X", 
                Queue, Request, IoControlCode);

    // Get the device context
    device = WdfIoQueueGetDevice(Queue);
    deviceContext = DeviceGetContext(device);

    // Get input buffer
    if (InputBufferLength > 0) {
        status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &inputBuffer, NULL);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
                        "WdfRequestRetrieveInputBuffer failed %!STATUS!", status);
            WdfRequestComplete(Request, status);
            return;
        }
    }

    // Get output buffer
    if (OutputBufferLength > 0) {
        status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, &outputBuffer, NULL);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
                        "WdfRequestRetrieveOutputBuffer failed %!STATUS!", status);
            WdfRequestComplete(Request, status);
            return;
        }
    }

    // Dispatch based on IOCTL code
    switch (IoControlCode) {

        case IOCTL_HID_SEND_KEYBOARD_INPUT:
        {
            PKEYBOARD_INPUT_CMD keyCmd;

            if (InputBufferLength < sizeof(KEYBOARD_INPUT_CMD)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            keyCmd = (PKEYBOARD_INPUT_CMD)inputBuffer;

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE,
                        "Keyboard IOCTL: KeyCode=0x%X, KeyDown=%d",
                        keyCmd->KeyCode, keyCmd->KeyDown);

            // Build the HID keyboard report
            ULONG reportSize = 0;
            BuildKeyboardReport(keyCmd->KeyCode, keyCmd->KeyDown,
                               deviceContext->KeyboardReportBuffer,
                               &reportSize);

            // In a real implementation, you would:
            // 1. Queue this report to be sent to HID subsystem
            // 2. Signal any waiting reads
            // For now, we just mark success
            status = STATUS_SUCCESS;
            bytesTransferred = 0;
            break;
        }

        case IOCTL_HID_SEND_MOUSE_INPUT:
        {
            PMOUSE_INPUT_CMD mouseCmd;

            if (InputBufferLength < sizeof(MOUSE_INPUT_CMD)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            mouseCmd = (PMOUSE_INPUT_CMD)inputBuffer;

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE,
                        "Relative Mouse IOCTL: X=%d, Y=%d, Buttons=0x%X",
                        mouseCmd->RelativeX, mouseCmd->RelativeY, mouseCmd->ButtonState);

            // Build the HID mouse report
            ULONG reportSize = 0;
            BuildMouseReport(mouseCmd->RelativeX, mouseCmd->RelativeY,
                            mouseCmd->ButtonState,
                            deviceContext->MouseReportBuffer,
                            &reportSize);

            status = STATUS_SUCCESS;
            bytesTransferred = 0;
            break;
        }

        case IOCTL_HID_SEND_ABS_MOUSE_INPUT:
        {
            PABS_MOUSE_INPUT_CMD absMouseCmd;

            if (InputBufferLength < sizeof(ABS_MOUSE_INPUT_CMD)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            absMouseCmd = (PABS_MOUSE_INPUT_CMD)inputBuffer;

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE,
                        "Absolute Mouse IOCTL: X=%u, Y=%u, Buttons=0x%X",
                        absMouseCmd->AbsoluteX, absMouseCmd->AbsoluteY, absMouseCmd->ButtonState);

            // Build the HID absolute mouse report
            ULONG reportSize = 0;
            BuildAbsMouseReport(absMouseCmd->AbsoluteX, absMouseCmd->AbsoluteY,
                               absMouseCmd->ButtonState,
                               deviceContext->MouseReportBuffer,
                               &reportSize);

            status = STATUS_SUCCESS;
            bytesTransferred = 0;
            break;
        }

        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    // Complete the request
    WdfRequestCompleteWithInformation(Request, status, bytesTransferred);

    return;
}

VOID
USBDriver1EvtIoStop(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG ActionFlags
)
/*++

Routine Description:

    This event is invoked for a power-managed queue before the device leaves the working state (D0).

Arguments:

    Queue -  Handle to the framework queue object that is associated with the
             I/O request.

    Request - Handle to a framework request object.

    ActionFlags - A bitwise OR of one or more WDF_REQUEST_STOP_ACTION_FLAGS-typed flags
                  that identify the reason that the callback function is being called
                  and whether the request is cancelable.

Return Value:

    VOID

--*/
{
    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "%!FUNC! Queue 0x%p, Request 0x%p ActionFlags %d", 
                Queue, Request, ActionFlags);

    //
    // In most cases, the EvtIoStop callback function completes, cancels, or postpones
    // further processing of the I/O request.
    //
    // Typically, the driver uses the following rules:
    //
    // - If the driver owns the I/O request, it either postpones further processing
    //   of the request and calls WdfRequestStopAcknowledge, or it calls WdfRequestComplete
    //   with a completion status value of STATUS_SUCCESS or STATUS_CANCELLED.
    //  
    //   The driver must call WdfRequestComplete only once, to either complete or cancel
    //   the request. To ensure that another thread does not call WdfRequestComplete
    //   for the same request, the EvtIoStop callback must synchronize with the driver's
    //   other event callback functions, for instance by using interlocked operations.
    //
    // - If the driver has forwarded the I/O request to an I/O target, it either calls
    //   WdfRequestCancelSentRequest to attempt to cancel the request, or it postpones
    //   further processing of the request and calls WdfRequestStopAcknowledge.
    //
    // A driver might choose to take no action in EvtIoStop for requests that are
    // guaranteed to complete in a small amount of time. For example, the driver might
    // take no action for requests that are completed in one of the driver抯 request handlers.
    //

    return;
}
