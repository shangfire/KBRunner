/*++

模块名称：

    device.h

模块说明：

    本文件定义了设备对象相关的所有数据结构和函数声明，核心是
    DEVICE_CONTEXT（设备上下文）结构体。

    在 KMDF 驱动中，"设备上下文"相当于 WDM 驱动中的"设备扩展"（Device Extension），
    是一块与设备对象关联的私有内存区域，用于存储驱动针对该设备实例的所有运行时状态。
    每个设备实例都有自己独立的 DEVICE_CONTEXT，多设备并发不会互相干扰。

运行环境：

    内核模式驱动框架（Kernel-Mode Driver Framework，KMDF）

--*/

#ifndef _DEVICE_H_
#define _DEVICE_H_

#include "public.h"   // 用户态/内核态共享的接口定义（IOCTL 码、命令结构体）
#include "shared.h"   // 共享内存 IPC 相关的数据结构定义

EXTERN_C_START

//
// 设备上下文结构体（Device Context）
//
// 本结构体存储虚拟 HID 键盘/鼠标设备的所有运行时状态数据。
// 在 WDM 驱动中这通常被称为"设备扩展"（Device Extension），
// WDF 通过 WDF_DECLARE_CONTEXT_TYPE_WITH_NAME 宏将其绑定到 WDFDEVICE 对象上。
//
// 访问方式：调用下方宏生成的 DeviceGetContext(device) 函数获取本结构体的指针。
//
typedef struct _DEVICE_CONTEXT
{
    WDFDEVICE Device;                       // WDF 设备对象句柄，指向本设备自身，
                                            // 在某些回调中可能需要反向引用设备对象

    // -------------------------------------------------------------------------
    // 虚拟 HID 设备描述符信息
    // HID 描述符告诉系统"这是什么类型的输入设备"以及"报文格式是什么"
    // -------------------------------------------------------------------------
    PVOID HidDescriptor;                    // 指向 HID 类描述符（HID Class Descriptor）的指针，
                                            // 包含 bcdHID 版本、国家代码、子描述符数量等信息
    PUCHAR HidReportDescriptor;             // 指向 HID 报告描述符（Report Descriptor）的指针。
                                            // 报告描述符是 HID 协议的核心，它用一种特殊的字节序列
                                            // 描述设备能发送的所有数据字段（如按键、坐标、滚轮等）
    ULONG HidReportDescriptorLength;        // HID 报告描述符的字节长度

    // -------------------------------------------------------------------------
    // 共享内存 IPC（进程间通信）字段
    // 用于与用户态的 Lua 脚本应用程序通信（当前仅声明，尚未完全实现）
    // -------------------------------------------------------------------------
    HANDLE SharedMemoryHandle;              // 内核对象句柄，指向已创建的共享内存节（Section Object）。
                                            // 通过 ZwCreateSection/ZwOpenSection 创建，
                                            // 用户态和内核态分别映射到各自的地址空间
    PSHARED_BUFFER SharedBuffer;            // 内核态中映射到的共享内存虚拟地址。
                                            // 用户态写入命令，驱动从这里读取并执行
    SIZE_T SharedBufferSize;                // 共享内存区域的总字节大小

    // -------------------------------------------------------------------------
    // HID 报文缓冲区
    // 存储最近一次发送给 HID 子系统的键盘/鼠标报文数据
    // -------------------------------------------------------------------------
    UCHAR KeyboardReportBuffer[8];          // 标准 USB 键盘 HID 报文缓冲区（固定 8 字节）：
                                            //   Byte 0：修饰键位掩码（Ctrl/Shift/Alt/Win 等）
                                            //   Byte 1：保留字节，固定为 0x00
                                            //   Byte 2-7：最多 6 个同时按下的键的 HID 键码
    ULONG KeyboardReportSize;               // 键盘报文有效字节数（通常为 8）

    UCHAR MouseReportBuffer[8];             // 标准 USB 鼠标 HID 报文缓冲区（最大 8 字节）：
                                            //   相对鼠标：[按键位][X偏移][Y偏移][滚轮] = 4 字节
                                            //   绝对鼠标：[按键位][X低][X高][Y低][Y高] = 5 字节
    ULONG MouseReportSize;                  // 鼠标报文有效字节数（相对 4 字节，绝对 5 字节）

    // -------------------------------------------------------------------------
    // 同步对象
    // 在内核中多个线程/中断可能同时访问设备数据，必须使用同步机制保护共享状态
    // -------------------------------------------------------------------------
    KSPIN_LOCK CommandLock;                 // 自旋锁（Spin Lock），用于保护命令处理流程中的竞态条件。
                                            // 自旋锁是内核中最轻量的同步原语，适合保护极短的临界区。
                                            // 注意：持有自旋锁时 IRQL 会被提升到 DISPATCH_LEVEL，
                                            // 因此持锁期间不能调用任何可能阻塞的函数
    HANDLE CommandEvent;                    // 内核事件对象句柄，当用户态写入新命令到共享内存后
                                            // 触发此事件，通知内核处理线程有新命令待处理

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

//
// WDF 设备上下文访问器宏展开
//
// WDF_DECLARE_CONTEXT_TYPE_WITH_NAME 宏会生成一个内联函数：
//     PDEVICE_CONTEXT DeviceGetContext(WDFDEVICE device);
//
// 使用示例：
//     PDEVICE_CONTEXT ctx = DeviceGetContext(Device);
//     ctx->KeyboardReportSize = 8;
//
// 这比直接用 WdfObjectGetTypedContext 更简洁，且具有编译期类型安全检查。
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

//
// 设备创建函数声明
//
// 在 DriverEntry → KMDriverEvtDeviceAdd 调用链中，
// KMDriverCreateDevice 是实际执行设备初始化的工作函数，
// 定义在 Device.c 中。
//
NTSTATUS
KMDriverCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit  // 设备初始化配置结构体，在此函数中被消耗（consumed）
    );

//
// 硬件准备回调函数声明
//
// EVT_WDF_DEVICE_PREPARE_HARDWARE 是 WDF 定义的标准回调类型。
// 当设备进入工作状态（D0 电源状态）且硬件资源已分配时，
// WDF 框架调用此回调，驱动在这里完成依赖于硬件资源的初始化工作
// （如映射 I/O 端口、建立共享内存等）。
//
EVT_WDF_DEVICE_PREPARE_HARDWARE KMDriverEvtDevicePrepareHardware;

EXTERN_C_END

#endif // _DEVICE_H_
