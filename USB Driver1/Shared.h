/*++

Module Name:

	shared.h

Abstract:

	This file contains shared data structures between user-mode applications
	and the kernel-mode HID driver for communicating keyboard/mouse commands.

Environment:

	Both user and kernel mode

--*/

#ifndef _SHARED_H_
#define _SHARED_H_

#include <ntdef.h>

//
// Shared memory constants
//
#define SHARED_MEMORY_NAME L"Global\\VirtualHIDSharedMemory"
#define SHARED_MEMORY_SIZE 0x1000  // 4 KB shared memory buffer

//
// Command types for the shared buffer
//
#define HID_CMD_TYPE_KEYBOARD 1
#define HID_CMD_TYPE_MOUSE_REL 2
#define HID_CMD_TYPE_MOUSE_ABS 3
#define HID_CMD_TYPE_SYNC 4      // Synchronization/handshake

//
// Shared command header structure
// This is placed at the beginning of each command in the shared buffer
//
typedef struct _SHARED_HID_CMD_HEADER {
	ULONG CommandType;    // HID_CMD_TYPE_* 
	ULONG CommandSize;    // Size of the command data following this header
	ULONG Timestamp;      // Timestamp for ordering commands
} SHARED_HID_CMD_HEADER, *PSHARED_HID_CMD_HEADER;

//
// Shared keyboard command (used in shared memory)
//
typedef struct _SHARED_KEYBOARD_CMD {
	SHARED_HID_CMD_HEADER Header;
	UCHAR KeyCode;        // Virtual key code
	BOOLEAN KeyDown;      // TRUE for press, FALSE for release
} SHARED_KEYBOARD_CMD, *PSHARED_KEYBOARD_CMD;

//
// Shared relative mouse command
//
typedef struct _SHARED_MOUSE_REL_CMD {
	SHARED_HID_CMD_HEADER Header;
	SHORT RelativeX;
	SHORT RelativeY;
	USHORT ButtonState;
} SHARED_MOUSE_REL_CMD, *PSHARED_MOUSE_REL_CMD;

//
// Shared absolute mouse command
//
typedef struct _SHARED_MOUSE_ABS_CMD {
	SHARED_HID_CMD_HEADER Header;
	USHORT AbsoluteX;
	USHORT AbsoluteY;
	USHORT ButtonState;
} SHARED_MOUSE_ABS_CMD, *PSHARED_MOUSE_ABS_CMD;

//
// Shared buffer structure for IPC
// This is mapped into both user and kernel address space
//
typedef struct _SHARED_BUFFER {
	ULONG WriteIndex;     // Index where user-mode writes next command
	ULONG ReadIndex;      // Index where kernel-mode reads next command
	ULONG BufferSize;     // Total size of command buffer
	LONG Lock;            // Simple spinlock (0=unlocked, 1=locked)
	// Commands follow after this header
	UCHAR CommandBuffer[SHARED_MEMORY_SIZE - 64]; // Reserve 64 bytes for header
} SHARED_BUFFER, *PSHARED_BUFFER;

#endif // _SHARED_H_
