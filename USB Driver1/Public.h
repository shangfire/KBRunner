/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

#ifndef _PUBLIC_H_
#define _PUBLIC_H_

//
// Define an Interface Guid so that app can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_USBDriver1,
    0x828e3dde,0xf615,0x49cc,0xa1,0x20,0x2d,0xd4,0x7e,0xcd,0x2c,0xd5);
// {828e3dde-f615-49cc-a120-2dd47ecd2cd5}

//
// IOCTL Codes for the virtual HID keyboard/mouse device
//
#define FILE_DEVICE_VIRTUAL_HID 0x8000

#define IOCTL_HID_SEND_KEYBOARD_INPUT \
    CTL_CODE(FILE_DEVICE_VIRTUAL_HID, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_HID_SEND_MOUSE_INPUT \
    CTL_CODE(FILE_DEVICE_VIRTUAL_HID, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_HID_SEND_ABS_MOUSE_INPUT \
    CTL_CODE(FILE_DEVICE_VIRTUAL_HID, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

//
// Keyboard input command structure
// Sent from user-mode application to kernel driver
//
typedef struct _KEYBOARD_INPUT_CMD {
    UCHAR KeyCode;           // Virtual key code (VK_A, VK_RETURN, etc.)
    BOOLEAN KeyDown;         // TRUE for key press, FALSE for key release
} KEYBOARD_INPUT_CMD, *PKEYBOARD_INPUT_CMD;

//
// Relative mouse input command structure
// For incremental mouse movement
//
typedef struct _MOUSE_INPUT_CMD {
    SHORT RelativeX;         // Relative X movement (-32768 to 32767)
    SHORT RelativeY;         // Relative Y movement (-32768 to 32767)
    USHORT ButtonState;      // 0=no button, 1=left, 2=right, 4=middle
} MOUSE_INPUT_CMD, *PMOUSE_INPUT_CMD;

//
// Absolute mouse input command structure
// For absolute positioning (useful for gamers)
//
typedef struct _ABS_MOUSE_INPUT_CMD {
    USHORT AbsoluteX;        // Absolute X position (0 to 65535)
    USHORT AbsoluteY;        // Absolute Y position (0 to 65535)
    USHORT ButtonState;      // 0=no button, 1=left, 2=right, 4=middle
} ABS_MOUSE_INPUT_CMD, *PABS_MOUSE_INPUT_CMD;

#endif // _PUBLIC_H_
