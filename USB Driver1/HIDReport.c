/*++

Module Name:

	hidreport.c

Abstract:

	This file contains functions for encoding HID keyboard and mouse reports.
	HID reports are the standardized format for sending input events to the system.

Environment:

	Kernel-mode Driver Framework

--*/

#include "driver.h"

//
// HID Keyboard report format (8 bytes):
// Byte 0: Modifier keys (Shift, Ctrl, Alt, etc.)
// Byte 1: Reserved (0x00)
// Bytes 2-7: Key codes for up to 6 simultaneous key presses
//
// Modifier key bits:
// 0x01 = Left Ctrl
// 0x02 = Left Shift
// 0x04 = Left Alt
// 0x08 = Left GUI (Windows key)
// 0x10 = Right Ctrl
// 0x20 = Right Shift
// 0x40 = Right Alt
// 0x80 = Right GUI
//
// Key codes are from the HID Usage Table for keyboard
//

//
// Standard HID modifier key mappings for Virtual Key codes
//
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LSHIFT   0xA0
#define VK_RSHIFT   0xA1
#define VK_LMENU    0xA4
#define VK_RMENU    0xA5
#define VK_LWIN     0x5B
#define VK_RWIN     0x5C
#define VK_RETURN   0x0D
#define VK_ESCAPE   0x1B
#define VK_BACK     0x08
#define VK_TAB      0x09
#define VK_SPACE    0x20
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PLUS  0xBB
#define VK_OEM_4     0xDB
#define VK_OEM_6     0xDD
#define VK_OEM_5     0xDC
#define VK_OEM_1     0xBA
#define VK_OEM_7     0xDE
#define VK_OEM_3     0xC0
#define VK_OEM_COMMA 0xBC
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2     0xBF
#define VK_CAPITAL   0x14
#define VK_F1        0x70
#define VK_F2        0x71
#define VK_F3        0x72
#define VK_F4        0x73
#define VK_F5        0x74
#define VK_F6        0x75
#define VK_F7        0x76
#define VK_F8        0x77
#define VK_F9        0x78
#define VK_F10       0x79
#define VK_F11       0x7A
#define VK_F12       0x7B
#define VK_HOME      0x24
#define VK_PRIOR     0x21
#define VK_DELETE    0x2E
#define VK_END       0x23
#define VK_NEXT      0x22
#define VK_RIGHT     0x27
#define VK_LEFT      0x25
#define VK_DOWN      0x28
#define VK_UP        0x26

//
// Function to convert Windows Virtual Key to HID key code
//
UCHAR VirtualKeyToHidCode(UCHAR VirtualKey)
{
	// Using if/else chain instead of switch for non-constant expressions
	// This is a simplified mapping - in production, you'd need a complete table
	// Here are some common mappings:

	if (VirtualKey >= 'A' && VirtualKey <= 'Z') {
		return 0x04 + (VirtualKey - 'A');
	}
	if (VirtualKey >= 'a' && VirtualKey <= 'z') {
		return 0x04 + (VirtualKey - 'a');
	}
	if (VirtualKey >= '1' && VirtualKey <= '9') {
		return 0x1E + (VirtualKey - '1');
	}
	if (VirtualKey == '0') {
		return 0x27;
	}

	// Now handle the VK_* constants which can be non-constant expressions
	if (VirtualKey == VK_RETURN) return 0x28;   // Enter
	if (VirtualKey == VK_ESCAPE) return 0x29;   // Escape
	if (VirtualKey == VK_BACK) return 0x2A;     // Backspace
	if (VirtualKey == VK_TAB) return 0x2B;      // Tab
	if (VirtualKey == VK_SPACE) return 0x2C;    // Space
	if (VirtualKey == VK_OEM_MINUS) return 0x2D;  // Minus
	if (VirtualKey == VK_OEM_PLUS) return 0x2E;   // Equals
	if (VirtualKey == VK_OEM_4) return 0x2F;    // Left bracket
	if (VirtualKey == VK_OEM_6) return 0x30;    // Right bracket
	if (VirtualKey == VK_OEM_5) return 0x31;    // Backslash
	if (VirtualKey == VK_OEM_1) return 0x33;    // Semicolon
	if (VirtualKey == VK_OEM_7) return 0x34;    // Apostrophe
	if (VirtualKey == VK_OEM_3) return 0x35;    // Tilde
	if (VirtualKey == VK_OEM_COMMA) return 0x36; // Comma
	if (VirtualKey == VK_OEM_PERIOD) return 0x37; // Period
	if (VirtualKey == VK_OEM_2) return 0x38;    // Slash
	if (VirtualKey == VK_CAPITAL) return 0x39;  // Caps lock

	// Function keys - F1 through F12 map to 0x3A through 0x45
	if (VirtualKey == VK_F1) return 0x3A;
	if (VirtualKey == VK_F2) return 0x3B;
	if (VirtualKey == VK_F3) return 0x3C;
	if (VirtualKey == VK_F4) return 0x3D;
	if (VirtualKey == VK_F5) return 0x3E;
	if (VirtualKey == VK_F6) return 0x3F;
	if (VirtualKey == VK_F7) return 0x40;
	if (VirtualKey == VK_F8) return 0x41;
	if (VirtualKey == VK_F9) return 0x42;
	if (VirtualKey == VK_F10) return 0x43;
	if (VirtualKey == VK_F11) return 0x44;
	if (VirtualKey == VK_F12) return 0x45;

	// Navigation keys
	if (VirtualKey == VK_HOME) return 0x4A;
	if (VirtualKey == VK_PRIOR) return 0x4B;    // Page Up
	if (VirtualKey == VK_DELETE) return 0x4C;
	if (VirtualKey == VK_END) return 0x4D;
	if (VirtualKey == VK_NEXT) return 0x4E;     // Page Down
	if (VirtualKey == VK_RIGHT) return 0x4F;
	if (VirtualKey == VK_LEFT) return 0x50;
	if (VirtualKey == VK_DOWN) return 0x51;
	if (VirtualKey == VK_UP) return 0x52;

	return 0x00;  // No key
}

//
// Function to get modifier bits for a Virtual Key code
//
UCHAR GetModifierBits(UCHAR VirtualKey)
{
	if (VirtualKey == VK_LCONTROL) return 0x01;
	if (VirtualKey == VK_LSHIFT) return 0x02;
	if (VirtualKey == VK_LMENU) return 0x04;
	if (VirtualKey == VK_LWIN) return 0x08;
	if (VirtualKey == VK_RCONTROL) return 0x10;
	if (VirtualKey == VK_RSHIFT) return 0x20;
	if (VirtualKey == VK_RMENU) return 0x40;
	if (VirtualKey == VK_RWIN) return 0x80;
	return 0x00;
}

//
// Build a HID keyboard report
//
VOID BuildKeyboardReport(
	_In_ UCHAR KeyCode,
	_In_ BOOLEAN KeyDown,
	_Out_ PUCHAR ReportBuffer,
	_Inout_ PULONG ReportSize
)
/*++

Routine Description:

	Builds a HID keyboard report for a key press or release.

Arguments:

	KeyCode - The Windows Virtual Key code
	KeyDown - TRUE for key press, FALSE for key release
	ReportBuffer - Pointer to the 8-byte HID keyboard report buffer
	ReportSize - Pointer to report size (set to 8)

Return Value:

	None

--*/
{
	UCHAR hidCode;
	UCHAR modBits;

	// Get the HID code for this key
	hidCode = VirtualKeyToHidCode(KeyCode);
	modBits = GetModifierBits(KeyCode);

	if (KeyDown) {
		if (modBits != 0) {
			// This is a modifier key
			ReportBuffer[0] |= modBits;
		} else {
			// This is a regular key - add to first available slot
			for (int i = 2; i < 8; i++) {
				if (ReportBuffer[i] == 0x00) {
					ReportBuffer[i] = hidCode;
					break;
				}
			}
		}
	} else {
		// Key release
		if (modBits != 0) {
			ReportBuffer[0] &= ~modBits;
		} else {
			// Remove from key slots
			for (int i = 2; i < 8; i++) {
				if (ReportBuffer[i] == hidCode) {
					ReportBuffer[i] = 0x00;
					break;
				}
			}
		}
	}

	*ReportSize = 8;
}

//
// Build a HID relative mouse report
//
VOID BuildMouseReport(
	_In_ SHORT RelativeX,
	_In_ SHORT RelativeY,
	_In_ USHORT ButtonState,
	_Out_ PUCHAR ReportBuffer,
	_Inout_ PULONG ReportSize
)
/*++

Routine Description:

	Builds a HID mouse report for relative movement.

Arguments:

	RelativeX - Relative X movement (-127 to 127)
	RelativeY - Relative Y movement (-127 to 127)
	ButtonState - Button state (bit 0=left, bit 1=right, bit 2=middle)
	ReportBuffer - Pointer to the 8-byte HID mouse report buffer
	ReportSize - Pointer to report size (set to 8)

Return Value:

	None

--*/
{
	// Standard USB mouse report: [Buttons][X][Y][Wheel]
	// Clamp relative movement to signed byte range
	CHAR x = (RelativeX > 127) ? 127 : (RelativeX < -128) ? -128 : (CHAR)RelativeX;
	CHAR y = (RelativeY > 127) ? 127 : (RelativeY < -128) ? -128 : (CHAR)RelativeY;

	ReportBuffer[0] = (UCHAR)(ButtonState & 0x07);  // Button bits
	ReportBuffer[1] = (UCHAR)x;                      // X relative
	ReportBuffer[2] = (UCHAR)y;                      // Y relative
	ReportBuffer[3] = 0x00;                          // Wheel (optional)

	*ReportSize = 4;
}

//
// Build a HID absolute mouse report
//
VOID BuildAbsMouseReport(
	_In_ USHORT AbsoluteX,
	_In_ USHORT AbsoluteY,
	_In_ USHORT ButtonState,
	_Out_ PUCHAR ReportBuffer,
	_Inout_ PULONG ReportSize
)
/*++

Routine Description:

	Builds a HID mouse report for absolute positioning.

Arguments:

	AbsoluteX - Absolute X position (0-65535)
	AbsoluteY - Absolute Y position (0-65535)
	ButtonState - Button state (bit 0=left, bit 1=right, bit 2=middle)
	ReportBuffer - Pointer to the 8-byte HID mouse report buffer
	ReportSize - Pointer to report size (set to 8)

Return Value:

	None

--*/
{
	// Absolute mouse report: [Buttons][X-low][X-high][Y-low][Y-high]
	ReportBuffer[0] = (UCHAR)(ButtonState & 0x07);  // Button bits
	ReportBuffer[1] = (UCHAR)(AbsoluteX & 0xFF);    // X low byte
	ReportBuffer[2] = (UCHAR)((AbsoluteX >> 8) & 0xFF); // X high byte
	ReportBuffer[3] = (UCHAR)(AbsoluteY & 0xFF);    // Y low byte
	ReportBuffer[4] = (UCHAR)((AbsoluteY >> 8) & 0xFF); // Y high byte

	*ReportSize = 5;
}
