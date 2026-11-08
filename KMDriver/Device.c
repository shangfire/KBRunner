/*++

模块名称：

    device.c

模块说明：

    本文件实现设备对象的创建与初始化逻辑，是驱动程序的核心模块之一。
    包含以下两个主要函数：

    1. KMDriverCreateDevice：
       由 EvtDeviceAdd 调用，负责为新设备实例创建 WDF 设备对象，
       初始化设备上下文，建立设备接口，并启动 I/O 队列。

    2. KMDriverEvtDevicePrepareHardware：
       PnP 电源管理回调，在设备进入工作状态（D0）且资源就绪后被调用，
       负责初始化 HID 报文缓冲区和共享内存通信通道。

运行环境：

    内核模式驱动框架（Kernel-Mode Driver Framework，KMDF）

--*/

#include "driver.h"
// device.tmh 由 WPP 预处理器自动生成，提供本文件中 TraceEvents 宏所需的追踪元数据
#include "device.tmh"

#ifdef ALLOC_PRAGMA
// 将以下函数放置在可分页代码段（PAGE 段）：
// - 可分页代码在系统内存紧张时可以被交换到分页文件，节省内核非分页内存
// - 限制：放入 PAGE 段的函数不能在 IRQL >= DISPATCH_LEVEL 时被调用，
//          否则会触发蓝屏（IRQL_NOT_LESS_OR_EQUAL 或 PAGE_FAULT_IN_NONPAGED_AREA）
#pragma alloc_text (PAGE, KMDriverCreateDevice)
#pragma alloc_text (PAGE, KMDriverEvtDevicePrepareHardware)
#endif


NTSTATUS
KMDriverCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit  // 设备初始化配置结构体，在 WdfDeviceCreate 成功后
                                         // 由框架自动释放，此后不可再访问
    )
/*++

函数说明：

    KMDriverCreateDevice 是设备创建的工作函数，由 EvtDeviceAdd 回调调用。
    本函数按以下步骤完成设备初始化：

    步骤 1：注册 PnP/电源事件回调
    步骤 2：初始化设备对象属性（绑定 DEVICE_CONTEXT 类型）
    步骤 3：调用 WdfDeviceCreate 创建 WDF 设备对象
    步骤 4：初始化设备上下文字段（自旋锁、报文缓冲区大小等）
    步骤 5：创建设备接口（Device Interface），让用户态程序能通过 GUID 找到设备
    步骤 6：初始化 I/O 队列（接收来自用户态的 IOCTL 请求）

参数说明：

    DeviceInit - 指向不透明的设备初始化结构体。在调用 WdfDeviceCreate 之前，
                 可以使用 WdfDeviceInitSetXxx 系列函数对其进行配置（例如指定
                 设备类型、I/O 类型、安全描述符等）。
                 WdfDeviceCreate 成功后，框架接管并释放该结构体，调用方不得再使用。

返回值：

    STATUS_SUCCESS        - 设备创建和初始化全部成功
    其他 NTSTATUS 错误码  - 某一步骤失败，框架会自动回滚已创建的部分资源

--*/
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;  // PnP/电源管理事件回调集合
    WDF_OBJECT_ATTRIBUTES deviceAttributes;          // 设备对象属性（用于指定上下文类型等）
    PDEVICE_CONTEXT deviceContext;                   // 指向设备上下文内存的指针
    WDFDEVICE device;                                // 创建成功后的 WDF 设备对象句柄
    NTSTATUS status;                                 // 各步骤操作结果状态码

    PAGED_CODE();  // 运行时断言：确保当前 IRQL <= APC_LEVEL，本函数才能安全执行

    //
    // 步骤 1：初始化 PnP/电源管理回调结构体，并注册回调函数。
    //
    // WDF_PNPPOWER_EVENT_CALLBACKS 包含一系列可选的回调槽位（如 EvtDeviceD0Entry、
    // EvtDeviceD0Exit、EvtDevicePrepareHardware 等），未注册的槽位由框架提供默认处理。
    // 这里仅注册 EvtDevicePrepareHardware，在设备硬件就绪时完成资源初始化。
    //
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = KMDriverEvtDevicePrepareHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    //
    // 步骤 2：初始化设备对象属性，将 DEVICE_CONTEXT 类型绑定到设备对象。
    //
    // WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE 宏展开后会：
    // - 设置 ContextSizeOverride 为 sizeof(DEVICE_CONTEXT)
    // - 设置 ContextTypeInfo 指向 DEVICE_CONTEXT 对应的类型描述符
    // 这样 WdfDeviceCreate 创建设备对象时，会自动分配并清零该上下文内存。
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    //
    // 步骤 3：创建 WDF 设备对象。
    //
    // WdfDeviceCreate 将：
    // - 使用 DeviceInit 中的配置创建底层 WDM 设备对象（DEVICE_OBJECT）
    // - 分配并初始化 WDF 包装层的设备对象（WDFDEVICE）
    // - 为设备对象分配 DEVICE_CONTEXT 大小的上下文内存，并清零
    // - 注册已配置的 PnP/电源回调函数
    //
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

    if (NT_SUCCESS(status)) {
        //
        // 步骤 4：初始化设备上下文字段。
        //
        // DeviceGetContext 是由 WDF_DECLARE_CONTEXT_TYPE_WITH_NAME 宏生成的
        // 类型安全的内联访问函数，返回指向 DEVICE_CONTEXT 内存的指针。
        //
        deviceContext = DeviceGetContext(device);
        deviceContext->Device = device;  // 保存设备对象句柄，供其他回调函数反向引用

        //
        // 初始化自旋锁（KSPIN_LOCK）。
        // 自旋锁是内核中保护共享数据的最基础同步原语，获取锁时会将 IRQL 提升到
        // DISPATCH_LEVEL，阻止当前处理器上的线程调度，从而保证临界区的原子性。
        // 必须在使用前显式初始化，初始状态为"未锁定"。
        //
        KeInitializeSpinLock(&deviceContext->CommandLock);

        //
        // 设置 HID 报文大小。
        // 标准 USB HID 键盘报文为 8 字节（见 USB HID Usage Tables 规范）。
        // 标准 USB HID 鼠标报文为 4~5 字节（相对/绝对模式不同）。
        // 这里统一初始化为 8，具体发送时会按实际大小截取。
        //
        deviceContext->KeyboardReportSize = 8;
        deviceContext->MouseReportSize = 8;

        //
        // 步骤 5：创建设备接口（Device Interface）。
        //
        // 设备接口是驱动向外暴露的"服务端点"，通过 GUID 标识。
        // 用户态程序调用 SetupDiGetClassDevs + SetupDiEnumDeviceInterfaces 查找
        // 本设备，然后用 CreateFile 打开获取的符号链接路径，得到设备句柄。
        //
        // WdfDeviceCreateDeviceInterface 会在对象管理器（Object Manager）的
        // \??\GlobalRoot 命名空间下创建一个符号链接（Symbolic Link），
        // 指向本设备对象，用户态可通过该链接访问驱动。
        //
        status = WdfDeviceCreateDeviceInterface(
            device,
            &GUID_DEVINTERFACE_KMDriver,  // 在 Public.h 中定义的设备接口 GUID
            NULL                             // 引用字符串：NULL 表示不使用子接口，
                                             // 如果一个驱动需要暴露多个接口实例，
                                             // 可通过此参数区分（如 "Channel0"、"Channel1"）
        );

        if (NT_SUCCESS(status)) {
            //
            // 步骤 6：初始化 I/O 队列。
            //
            // I/O 队列负责接收和分发来自用户态的 I/O 请求（IRP），
            // 包括 DeviceIoControl（IOCTL）、ReadFile、WriteFile 等操作。
            // 具体的队列配置和 IOCTL 处理逻辑在 Queue.c 中实现。
            //
            status = KMDriverQueueInitialize(device);
        }
    }

    return status;
}

NTSTATUS
KMDriverEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,                      // WDF 设备对象句柄
    _In_ WDFCMRESLIST ResourceList,             // 原始硬件资源列表（I/O端口、内存地址、IRQ等）
    _In_ WDFCMRESLIST ResourceListTranslated    // 已转换的硬件资源列表（系统地址空间中的虚拟地址）
    )
/*++

函数说明：

    KMDriverEvtDevicePrepareHardware 是 PnP/电源管理回调，
    由 WDF 框架在以下时机调用：
    - 设备首次启动，进入工作状态（D0）之前
    - 设备从低功耗状态（D1/D2/D3）恢复到工作状态（D0）之前

    本函数负责完成依赖硬件资源的初始化工作：
    - 初始化 HID 报文缓冲区
    - 建立与用户态应用程序的共享内存通信通道（TODO：尚未完整实现）

    对于虚拟设备（本驱动没有真实硬件），ResourceList 通常为空，
    但此回调仍然是执行"设备启动时初始化"的合适位置。

参数说明：

    Device                - WDF 设备对象句柄，通过它可获取设备上下文
    ResourceList          - 原始资源列表（保持总线地址，适用于驱动直接访问总线资源的场景）
    ResourceListTranslated - 转换后的资源列表（已映射到系统虚拟地址空间，驱动通常使用此列表）

返回值：

    STATUS_SUCCESS        - 硬件准备成功，框架继续推进设备进入 D0 状态
    其他 NTSTATUS 错误码  - 准备失败，设备将无法进入工作状态

--*/
{
    NTSTATUS status;
    PDEVICE_CONTEXT pDeviceContext;
    UNICODE_STRING sharedMemoryName;  // 共享内存名称的 Unicode 字符串（内核 API 要求此格式）

    // 本驱动是虚拟设备，不使用实际的硬件资源列表，标记为未引用以消除编译器警告
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(ResourceListTranslated);

    PAGED_CODE();  // 运行时断言：确保当前 IRQL 允许分页代码执行

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = STATUS_SUCCESS;
    pDeviceContext = DeviceGetContext(Device);  // 获取本设备的私有上下文数据指针

    //
    // 初始化键盘 HID 报文缓冲区。
    //
    // 标准 USB 键盘报文格式（8 字节，来自 USB HID Usage Tables 规范）：
    //   Byte 0：修饰键位掩码（Modifier Keys）
    //           bit 0 = Left Ctrl,  bit 1 = Left Shift,  bit 2 = Left Alt,  bit 3 = Left GUI(Win键)
    //           bit 4 = Right Ctrl, bit 5 = Right Shift, bit 6 = Right Alt,  bit 7 = Right GUI
    //   Byte 1：保留字节，必须为 0x00（OEM 保留，当前协议版本不使用）
    //   Byte 2-7：同时按下的最多 6 个键的 HID 键码（Key Code）。
    //             键码为 0x00 表示该槽位空闲（未按下）。
    //             支持最多 6 键同时按下（N-Key Rollover 需要特殊描述符）。
    //
    // RtlZeroMemory 将缓冲区清零，表示"所有键均未按下"的初始状态。
    //
    RtlZeroMemory(pDeviceContext->KeyboardReportBuffer, sizeof(pDeviceContext->KeyboardReportBuffer));

    //
    // 初始化鼠标 HID 报文缓冲区。
    //
    // 相对鼠标报文格式（4 字节，Boot Protocol Mouse Report）：
    //   Byte 0：按键状态位掩码（bit 0 = 左键，bit 1 = 右键，bit 2 = 中键）
    //   Byte 1：X 轴相对位移（有符号字节，-128 ~ +127，正值向右）
    //   Byte 2：Y 轴相对位移（有符号字节，-128 ~ +127，正值向下）
    //   Byte 3：滚轮位移（有符号字节，-128 ~ +127，正值向前滚）
    //
    // 绝对鼠标报文格式（5 字节，Absolute Positioning Report）：
    //   Byte 0：按键状态位掩码（同上）
    //   Byte 1-2：X 轴绝对坐标低字节 + 高字节（UINT16 小端序，范围 0~65535）
    //   Byte 3-4：Y 轴绝对坐标低字节 + 高字节（UINT16 小端序，范围 0~65535）
    //
    RtlZeroMemory(pDeviceContext->MouseReportBuffer, sizeof(pDeviceContext->MouseReportBuffer));

    //
    // 初始化共享内存通信通道（TODO：完整实现待完成）
    //
    // 完整实现步骤：
    //   1. 调用 RtlInitUnicodeString 初始化共享内存名称字符串
    //   2. 调用 ZwCreateSection 创建命名内存节对象（Section Object），
    //      分配 SHARED_MEMORY_SIZE 字节的共享内存区域
    //   3. 调用 ZwMapViewOfSection 将共享内存映射到内核地址空间，
    //      获取内核侧的虚拟地址指针（pDeviceContext->SharedBuffer）
    //   4. 创建内核事件对象（ZwCreateEvent），用于用户态写入命令后通知内核
    //   5. 创建内核线程（PsCreateSystemThread）轮询事件并处理命令队列
    //
    // 当前仅初始化名称字符串，实际的节对象创建代码尚未实现：
    RtlInitUnicodeString(&sharedMemoryName, SHARED_MEMORY_NAME);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "Virtual HID device initialized successfully");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}
