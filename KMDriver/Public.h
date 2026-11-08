/*++

模块名称：

    public.h

模块说明：

    本文件定义了驱动程序与用户态应用程序之间共享的接口，包括：
    - 设备接口 GUID（用于用户态程序定位设备）
    - IOCTL 控制码（用户态通过 DeviceIoControl 发送命令的编号）
    - 命令数据结构体（在用户态和内核态之间传递的数据格式）

    凡是在"用户态应用程序"和"内核态驱动"两侧都会用到的定义，
    应当放在本文件中，以确保双方使用完全一致的数据布局。
    用户态项目只需 #include 本文件，无需引入任何内核专用头文件。

运行环境：

    用户态（User Mode）和内核态（Kernel Mode）均适用

--*/

#ifndef _PUBLIC_H_
#define _PUBLIC_H_

//
// 设备接口 GUID（Globally Unique Identifier，全局唯一标识符）
//
// 用户态应用程序通过以下流程打开本驱动设备：
//   1. 调用 SetupDiGetClassDevs(&GUID_DEVINTERFACE_KMDriver, ...) 枚举设备
//   2. 调用 SetupDiEnumDeviceInterfaces 获取设备路径
//   3. 调用 CreateFile(路径, ...) 获取设备句柄
//   4. 用句柄调用 DeviceIoControl 发送 IOCTL 命令
//
// 每个驱动都应有自己唯一的 GUID，可以用 guidgen.exe 或 Visual Studio 的
// "工具 → 创建 GUID" 生成，确保不与其他驱动冲突。
//
DEFINE_GUID (GUID_DEVINTERFACE_KMDriver,
    0x828e3dde,0xf615,0x49cc,0xa1,0x20,0x2d,0xd4,0x7e,0xcd,0x2c,0xd5);
// {828e3dde-f615-49cc-a120-2dd47ecd2cd5}

//
// IOCTL（I/O Control Code，I/O 控制码）定义
//
// CTL_CODE 是系统提供的宏，用于将四个参数编码成一个 32 位的控制码：
//
//   参数 1 - DeviceType（设备类型）：
//     0x0000~0x7FFF 为微软保留，0x8000~0xFFFF 为第三方驱动使用。
//     本驱动使用 0x8000 表示"虚拟 HID 设备"。
//
//   参数 2 - Function（功能码）：
//     0x000~0x7FF 为微软保留，0x800~0xFFF 为第三方驱动使用。
//     本驱动从 0x801 开始依次分配各命令的功能码。
//
//   参数 3 - Method（缓冲区传输方式）：
//     METHOD_BUFFERED：系统自动将用户缓冲区内容拷贝到内核中间缓冲区，
//     驱动操作的是安全的内核缓冲区副本，无需手动验证用户态指针（最安全）。
//     METHOD_NEITHER：直接传递用户态指针，驱动需自行验证地址合法性（高性能但危险）。
//
//   参数 4 - Access（访问权限要求）：
//     FILE_ANY_ACCESS：调用方无需持有特定权限即可发送此 IOCTL，
//     适合不涉及敏感操作的命令（如果需要限制，可改为 FILE_WRITE_ACCESS）。
//
#define FILE_DEVICE_VIRTUAL_HID 0x8000
// 释放所有按键，防止脚本停止后残留输入。
#define IOCTL_KM_RELEASE_ALL CTL_CODE(FILE_DEVICE_VIRTUAL_HID, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
// 协议查询成功返回 ULONG 版本号，当前为 1。
#define IOCTL_KM_QUERY_STATUS CTL_CODE(FILE_DEVICE_VIRTUAL_HID, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)

// 键盘输入控制码：用户态发送一次按键事件（按下或松开一个键）
#define IOCTL_HID_SEND_KEYBOARD_INPUT \
    CTL_CODE(FILE_DEVICE_VIRTUAL_HID, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

// 相对鼠标输入控制码：用户态发送鼠标相对移动量和按键状态
#define IOCTL_HID_SEND_MOUSE_INPUT \
    CTL_CODE(FILE_DEVICE_VIRTUAL_HID, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

// 绝对鼠标输入控制码：用户态发送鼠标绝对屏幕坐标（适合精确定位场景）
#define IOCTL_HID_SEND_ABS_MOUSE_INPUT \
    CTL_CODE(FILE_DEVICE_VIRTUAL_HID, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

//
// 键盘输入命令结构体
//
// 用户态调用 DeviceIoControl(hDevice, IOCTL_HID_SEND_KEYBOARD_INPUT,
//   &cmd, sizeof(cmd), NULL, 0, &bytesReturned, NULL) 时，
// 将此结构体作为 lpInBuffer 传入，告知驱动模拟哪个键的按下或松开。
//
typedef struct _KEYBOARD_INPUT_CMD {
    UCHAR KeyCode;      // Windows 虚拟键码（Virtual Key Code）。
                        // 常用值：'A'~'Z'=0x41~0x5A，'0'~'9'=0x30~0x39，
                        // VK_RETURN=0x0D（回车），VK_LSHIFT=0xA0（左Shift），
                        // VK_LCONTROL=0xA2（左Ctrl），VK_SPACE=0x20（空格）。
                        // 完整列表参见 MSDN：Virtual-Key Codes。
    BOOLEAN KeyDown;    // 按键方向标志：
                        //   TRUE  = 按下（Key Press，对应 WM_KEYDOWN）
                        //   FALSE = 松开（Key Release，对应 WM_KEYUP）
} KEYBOARD_INPUT_CMD, *PKEYBOARD_INPUT_CMD;

//
// 相对鼠标输入命令结构体
//
// 相对移动描述鼠标相对于当前位置的偏移量，模拟普通物理鼠标的行为。
// 通过 IOCTL_HID_SEND_MOUSE_INPUT 传入驱动。
//
typedef struct _MOUSE_INPUT_CMD {
    SHORT RelativeX;    // X 轴相对移动量：正值向右，负值向左，范围 -32768 ~ 32767。
                        // 注意：HID 协议单次报文的 X/Y 字段为有符号字节（-128~+127），
                        // 驱动内部会自动截断超出范围的值，大范围移动需分多次发送。
    SHORT RelativeY;    // Y 轴相对移动量：正值向下，负值向上，范围 -32768 ~ 32767。
    USHORT ButtonState; // 鼠标按键状态位掩码（按下=1，松开=0）：
                        //   bit 0 (0x0001) = 左键
                        //   bit 1 (0x0002) = 右键
                        //   bit 2 (0x0004) = 中键（滚轮键）
                        //   0x0000 = 所有按键均未按下
} MOUSE_INPUT_CMD, *PMOUSE_INPUT_CMD;

//
// 绝对鼠标输入命令结构体
//
// 绝对坐标将鼠标定位到屏幕上的固定位置，0 表示最左/上，65535 表示最右/下。
// HID 子系统会根据实际屏幕分辨率自动换算像素坐标。
// 适合需要精确点击特定位置的场景（如自动化测试、游戏辅助脚本等）。
// 通过 IOCTL_HID_SEND_ABS_MOUSE_INPUT 传入驱动。
//
typedef struct _ABS_MOUSE_INPUT_CMD {
    USHORT AbsoluteX;   // X 轴绝对坐标：0 = 屏幕最左侧，65535 = 屏幕最右侧
    USHORT AbsoluteY;   // Y 轴绝对坐标：0 = 屏幕最上方，65535 = 屏幕最下方
    USHORT ButtonState; // 鼠标按键状态位掩码（含义与 MOUSE_INPUT_CMD.ButtonState 相同）
} ABS_MOUSE_INPUT_CMD, *PABS_MOUSE_INPUT_CMD;

#endif // _PUBLIC_H_
