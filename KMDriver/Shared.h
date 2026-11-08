/*++

模块名称：

    shared.h

模块说明：

    本文件定义了内核态驱动与用户态应用程序之间通过共享内存进行
    进程间通信（IPC，Inter-Process Communication）所使用的数据结构。

    通信架构：
    ┌─────────────────────┐    共享内存（Section Object）    ┌──────────────────────┐
    │  用户态应用（Lua脚本）│ ──────────────────────────────> │  内核态驱动（本驱动）  │
    │  写入 HID 命令        │    SHARED_BUFFER（环形缓冲区）   │  读取命令并模拟输入    │
    └─────────────────────┘                                  └──────────────────────┘

    本文件在用户态和内核态均可包含，因此只能使用两侧都支持的类型定义。

运行环境：

    用户态（User Mode）和内核态（Kernel Mode）均适用

--*/

#ifndef _SHARED_H_
#define _SHARED_H_

#include <ntdef.h>  // 提供 ULONG、USHORT、LONG 等在用户/内核两侧通用的基础类型

//
// 共享内存配置常量
//

// 共享内存的 NT 命名空间路径。
// "Global\" 前缀表示该对象在全局会话（Session 0）中可见，
// 即使用户态程序在普通用户会话中运行，也能通过此名称访问驱动创建的共享内存。
// 注意：创建带 Global\ 前缀的对象通常需要 SeCreateGlobalPrivilege 权限。
#define SHARED_MEMORY_NAME L"Global\\VirtualHIDSharedMemory"

// 共享内存总大小：4 KB（0x1000 字节）。
// 这是一个合理的初始值，足以缓冲若干条命令。
// 如果需要更高的命令吞吐量，可以适当增大此值。
#define SHARED_MEMORY_SIZE 0x1000

//
// HID 命令类型常量
//
// 每条写入共享缓冲区的命令都以 SHARED_HID_CMD_HEADER 开头，
// 其中 CommandType 字段使用以下常量标识命令类型。
//
#define HID_CMD_TYPE_KEYBOARD  1  // 键盘按键事件（按下或松开某个键）
#define HID_CMD_TYPE_MOUSE_REL 2  // 鼠标相对移动事件（相对当前位置的偏移量）
#define HID_CMD_TYPE_MOUSE_ABS 3  // 鼠标绝对定位事件（屏幕上的绝对坐标）
#define HID_CMD_TYPE_SYNC      4  // 同步/握手命令（用于验证通信通道是否正常）

//
// 共享命令头部结构体
//
// 每一条写入共享缓冲区的命令都以此结构体开头，
// 内核态驱动首先读取头部以判断命令类型和大小，
// 然后再读取紧随其后的命令数据体。
//
typedef struct _SHARED_HID_CMD_HEADER {
    ULONG CommandType;   // 命令类型，取值为 HID_CMD_TYPE_* 常量之一
    ULONG CommandSize;   // 命令数据体的字节大小（不含本头部结构体自身的大小），
                         // 驱动据此跳过当前命令，定位到缓冲区中下一条命令的起始位置
    ULONG Timestamp;     // 命令时间戳（毫秒数或任意单调递增序列号），
                         // 用于在批量命令时保证处理顺序，防止乱序执行
} SHARED_HID_CMD_HEADER, *PSHARED_HID_CMD_HEADER;

//
// 共享内存中的键盘命令结构体
//
// 用户态向共享缓冲区写入此结构体，表示模拟一次键盘按键事件。
// Header.CommandType 应设置为 HID_CMD_TYPE_KEYBOARD。
//
typedef struct _SHARED_KEYBOARD_CMD {
    SHARED_HID_CMD_HEADER Header;  // 通用命令头部，驱动通过它识别命令类型
    UCHAR KeyCode;                 // Windows 虚拟键码（Virtual Key Code），
                                   // 例如：'A'=0x41, VK_RETURN=0x0D, VK_LSHIFT=0xA0
    BOOLEAN KeyDown;               // 按键方向：TRUE = 按下，FALSE = 松开
} SHARED_KEYBOARD_CMD, *PSHARED_KEYBOARD_CMD;

//
// 共享内存中的鼠标相对移动命令结构体
//
// 相对移动命令描述鼠标相对于当前位置的位移量，模拟普通鼠标移动行为。
// Header.CommandType 应设置为 HID_CMD_TYPE_MOUSE_REL。
//
typedef struct _SHARED_MOUSE_REL_CMD {
    SHARED_HID_CMD_HEADER Header;  // 通用命令头部
    SHORT RelativeX;               // X 轴位移量：正值向右，负值向左
    SHORT RelativeY;               // Y 轴位移量：正值向下，负值向上
    USHORT ButtonState;            // 鼠标按键状态位掩码：
                                   //   bit 0 = 左键，bit 1 = 右键，bit 2 = 中键
} SHARED_MOUSE_REL_CMD, *PSHARED_MOUSE_REL_CMD;

//
// 共享内存中的鼠标绝对定位命令结构体
//
// 绝对定位命令将鼠标移动到屏幕上的指定绝对坐标，
// 坐标范围 0~65535 会被 HID 子系统映射到实际屏幕分辨率。
// Header.CommandType 应设置为 HID_CMD_TYPE_MOUSE_ABS。
//
typedef struct _SHARED_MOUSE_ABS_CMD {
    SHARED_HID_CMD_HEADER Header;  // 通用命令头部
    USHORT AbsoluteX;              // X 轴绝对坐标（0 = 最左，65535 = 最右）
    USHORT AbsoluteY;              // Y 轴绝对坐标（0 = 最上，65535 = 最下）
    USHORT ButtonState;            // 鼠标按键状态位掩码（同 SHARED_MOUSE_REL_CMD）
} SHARED_MOUSE_ABS_CMD, *PSHARED_MOUSE_ABS_CMD;

//
// 共享缓冲区结构体（环形缓冲区头部 + 命令数据区）
//
// 这块内存同时被映射到用户态和内核态的地址空间，实现零拷贝的高性能 IPC。
//
// 生产者/消费者模型：
//   - 用户态应用（生产者）：向 CommandBuffer[WriteIndex] 写入命令，然后递增 WriteIndex
//   - 内核态驱动（消费者）：从 CommandBuffer[ReadIndex] 读取命令，然后递增 ReadIndex
//   - 当 WriteIndex == ReadIndex 时，缓冲区为空（无待处理命令）
//
// 注意：对 WriteIndex/ReadIndex 的访问需要通过 Lock 字段进行同步，
// 防止用户态写入与内核态读取产生竞态条件。
//
typedef struct _SHARED_BUFFER {
    ULONG WriteIndex;   // 写入索引：用户态下一次写入命令的起始偏移（相对于 CommandBuffer）
    ULONG ReadIndex;    // 读取索引：内核态下一次读取命令的起始偏移（相对于 CommandBuffer）
    ULONG BufferSize;   // CommandBuffer 的总字节数（便于计算环绕边界）
    LONG  Lock;         // 简易自旋锁：0 表示未锁定，1 表示已锁定。
                        // 用户态通过 InterlockedCompareExchange 原子操作实现加锁/解锁，
                        // 内核态使用 InterlockedCompareExchange 或 KSPIN_LOCK 对应机制。
                        // 警告：这种用户态/内核态共享的简易锁存在安全风险，
                        //       恶意用户态程序可能破坏锁状态，生产环境应考虑更安全的方案
    // 命令数据区（紧跟在头部字段之后）
    // 头部保留 64 字节对齐空间（WriteIndex + ReadIndex + BufferSize + Lock = 16 字节，
    // 其余 48 字节为未来扩展预留），实际命令从 CommandBuffer[0] 开始存放
    UCHAR CommandBuffer[SHARED_MEMORY_SIZE - 64];
} SHARED_BUFFER, *PSHARED_BUFFER;

#endif // _SHARED_H_
