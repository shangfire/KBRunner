/*++

Module Name:

	hidreport.h

Abstract:

	This file contains declarations for HID report generation functions.

Environment:

	Kernel-mode Driver Framework

--*/

#ifndef _HIDREPORT_H_
#define _HIDREPORT_H_

EXTERN_C_START

//
// Function declarations for HID report building
//

VOID BuildKeyboardReport(
	_In_ UCHAR KeyCode,
	_In_ BOOLEAN KeyDown,
	_Out_ PUCHAR ReportBuffer,
	_Inout_ PULONG ReportSize
);

VOID BuildMouseReport(
	_In_ SHORT RelativeX,
	_In_ SHORT RelativeY,
	_In_ USHORT ButtonState,
	_Out_ PUCHAR ReportBuffer,
	_Inout_ PULONG ReportSize
);

VOID BuildAbsMouseReport(
	_In_ USHORT AbsoluteX,
	_In_ USHORT AbsoluteY,
	_In_ USHORT ButtonState,
	_Out_ PUCHAR ReportBuffer,
	_Inout_ PULONG ReportSize
);

EXTERN_C_END

#endif // _HIDREPORT_H_
