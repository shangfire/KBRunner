/*++

模块名称：

    driver.h

模块说明：

    本文件是整个驱动程序的主头文件，所有 .c 源文件都会通过 #include "driver.h" 引入它。
    它的职责是集中引入以下内容：
    - 操作系统内核和 WDF 框架所需的系统头文件
    - USB 相关的系统头文件
    - 本驱动自己的子模块头文件（device.h、queue.h、trace.h）
    - 驱动程序生命周期函数的前向声明

    通过统一在此文件中管理 include，可以避免各 .c 文件重复包含头文件，
    也保证了整个驱动使用一致的类型定义和宏定义。

运行环境：

    内核模式驱动框架（Kernel-Mode Driver Framework，KMDF）

--*/

// ntddk.h：Windows 驱动开发的核心头文件（NT Driver Development Kit）。
// 提供内核基础类型（NTSTATUS、UNICODE_STRING、PVOID 等）、
// 内存管理 API（ExAllocatePool）、同步原语（KeInitializeSpinLock）等。
#include <ntddk.h>

// wdf.h：WDF（Windows Driver Framework）框架头文件。
// 提供 WDFDRIVER、WDFDEVICE、WDFREQUEST 等框架对象类型，
// 以及 WdfDriverCreate、WdfDeviceCreate 等框架 API。
// WDF 是微软推荐的现代内核驱动开发框架，大幅简化了 PnP 和电源管理逻辑。
#include <wdf.h>

// usb.h：USB 协议核心定义，包含 USB 描述符结构体
// （如 USB_DEVICE_DESCRIPTOR、USB_CONFIGURATION_DESCRIPTOR 等）。
#include <usb.h>

// usbdlib.h：USB 驱动库（USB Driver Library）函数声明，
// 提供操作 USB URB（USB Request Block）的辅助函数。
#include <usbdlib.h>

// wdfusb.h：WDF 框架针对 USB 设备的扩展头文件，
// 提供 WDFUSBDEVICE、WDFUSBTARGETPIPE 等 USB 专用对象类型。
#include <wdfusb.h>

// initguid.h：允许在本编译单元中"定义"（而非仅仅"声明"）GUID。
// 必须在包含任何定义了 DEFINE_GUID(...) 的头文件之前引入，
// 否则 GUID 只会被声明为外部符号，链接时会报"无法解析的外部符号"错误。
#include <initguid.h>

// device.h：设备对象相关定义。
// 包含 DEVICE_CONTEXT 设备上下文结构体，以及 KMDriverCreateDevice 函数声明。
#include "device.h"

// queue.h：I/O 队列相关定义。
// 包含 KMDriverQueueInitialize 初始化函数，以及 IOCTL 处理回调的声明。
#include "queue.h"

// trace.h：WPP 软件追踪（调试日志）相关宏定义。
// 定义了 TraceEvents() 宏和追踪标志位（TRACE_DRIVER、TRACE_DEVICE、TRACE_QUEUE）。
#include "trace.h"

EXTERN_C_START

//
// 驱动程序生命周期函数前向声明
//
// 这三个函数构成驱动程序的完整生命周期：
//   加载（DriverEntry） -> 设备插入（EvtDeviceAdd） -> 卸载（EvtDriverContextCleanup）
//

// DriverEntry：驱动程序入口点。
// 当系统将驱动加载到内存时，I/O 管理器首先调用此函数，
// 功能相当于用户态 C 程序的 main()，在这里完成 WDF 框架初始化。
DRIVER_INITIALIZE DriverEntry;

// KMDriverEvtDeviceAdd：设备添加事件回调。
// 当即插即用（PnP）管理器检测到与本驱动匹配的新设备时，
// WDF 框架自动调用此函数，通知驱动为该设备实例完成初始化。
// 每发现一个新设备实例就调用一次（支持多设备并发）。
EVT_WDF_DRIVER_DEVICE_ADD KMDriverEvtDeviceAdd;

// KMDriverEvtDriverContextCleanup：驱动对象清理回调。
// 在驱动卸载时由 WDF 框架调用，负责释放 DriverEntry 中分配的资源。
// 主要工作是调用 WPP_CLEANUP 停止调试追踪会话，防止资源泄漏。
EVT_WDF_OBJECT_CONTEXT_CLEANUP KMDriverEvtDriverContextCleanup;

EXTERN_C_END
