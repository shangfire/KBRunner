/*++

模块名称：

    driver.c

模块说明：

    本文件是整个驱动程序的入口模块，包含以下核心逻辑：
    1. DriverEntry：驱动加载时由操作系统调用的第一个函数，
       在此初始化 WDF 框架并注册后续的回调函数。
    2. KMDriverEvtDeviceAdd：每当系统发现一个与本驱动匹配的设备时，
       WDF 框架自动调用此函数，驱动在这里为该设备实例完成初始化。
    3. KMDriverEvtDriverContextCleanup：驱动卸载时的收尾工作，
       主要负责停止 WPP 软件追踪（内核态调试日志系统）。

运行环境：

    内核模式驱动框架（Kernel-Mode Driver Framework，KMDF）

--*/

#include "driver.h"
// driver.tmh 由 WPP 预处理器自动生成，包含追踪宏的展开代码，
// 必须在使用任何 TraceEvents 宏之前包含
#include "driver.tmh"

#ifdef ALLOC_PRAGMA
// ALLOC_PRAGMA 指示编译器将以下函数放置在特定的内存段中：
// "INIT" 段：驱动加载完成后该内存可以被系统回收，节省物理内存。
//            适合只在初始化时执行一次的代码（如 DriverEntry）。
// "PAGE" 段：该段代码可以被换出到分页内存池，在低内存压力时节省内核非分页内存。
//            注意：放入 PAGE 段的函数不能在 IRQL >= DISPATCH_LEVEL 时被调用！
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, KMDriverEvtDeviceAdd)
#pragma alloc_text (PAGE, KMDriverEvtDriverContextCleanup)
#endif


NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,   // 由系统分配的驱动对象指针，代表本驱动在内核中的实例
    _In_ PUNICODE_STRING RegistryPath    // 本驱动在注册表中的服务键路径，
                                         // 例如 \Registry\Machine\System\CurrentControlSet\Services\KMDriver
    )
/*++

函数说明：

    DriverEntry 是驱动程序的入口点，由 I/O 管理器（I/O Manager）在驱动加载时调用，
    执行顺序类似于用户态程序的 main() 函数。

    本函数完成三件事：
    1. 初始化 WPP 软件追踪（用于内核态调试输出，替代 DbgPrint）
    2. 配置 WDF 驱动对象（注册 EvtDeviceAdd 回调、设置清理回调）
    3. 调用 WdfDriverCreate 创建 WDF 驱动对象，完成框架初始化

    注意：DriverEntry 运行在 PASSIVE_LEVEL IRQL，可以使用所有内核服务。

参数说明：

    DriverObject  - 系统预先分配的驱动对象。DriverEntry 返回前必须对其完成初始化。
                    系统在驱动卸载时会自动释放该对象。
    RegistryPath  - 本驱动在注册表中的路径字符串（UNICODE 格式）。
                    驱动可以利用该路径在重启之间持久化保存配置数据。
                    注意：该路径指向驱动的服务键，不包含具体设备实例的信息。

返回值：

    STATUS_SUCCESS  - 初始化成功，驱动正常加载
    其他 NTSTATUS 错误码 - 初始化失败，系统将卸载驱动并报告错误

--*/
{
    WDF_DRIVER_CONFIG config;           // WDF 驱动配置结构体，用于指定 EvtDeviceAdd 等回调
    NTSTATUS status;                    // 各步骤的操作结果状态码
    WDF_OBJECT_ATTRIBUTES attributes;  // WDF 对象属性，用于指定清理回调等通用属性

    //
    // 初始化 WPP（Windows Software Trace Preprocessor）软件追踪
    // WPP 是内核态推荐的调试日志机制，比 DbgPrint 更高效，支持实时日志收集。
    // 必须在任何 TraceEvents 调用之前执行此初始化。
    //
    WPP_INIT_TRACING( DriverObject, RegistryPath );

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // 配置驱动对象属性，注册驱动清理回调函数。
    // 当 WDF 驱动对象被删除（即驱动卸载）时，框架会自动调用 EvtCleanupCallback，
    // 我们在其中执行 WPP_CLEANUP 来停止追踪会话，防止资源泄漏。
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = KMDriverEvtDriverContextCleanup;

    //
    // 初始化 WDF 驱动配置结构体，并指定"设备添加"回调函数。
    // 每当 PnP 管理器发现一个新的匹配设备时，框架就会调用 KMDriverEvtDeviceAdd。
    //
    WDF_DRIVER_CONFIG_INIT(&config,
                           KMDriverEvtDeviceAdd
                           );

    //
    // 创建 WDF 驱动对象。
    // WdfDriverCreate 是 WDF 框架初始化的核心步骤，它将：
    // - 把 WDM DriverObject 包装为 WDF WDFDRIVER 对象
    // - 注册所有已配置的回调函数
    // - 将驱动对象属性（包括清理回调）应用到新建的对象上
    // 最后一个参数 WDF_NO_HANDLE 表示不需要返回 WDFDRIVER 句柄（用不到时可省略）
    //
    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attributes,
                             &config,
                             WDF_NO_HANDLE
                             );

    if (!NT_SUCCESS(status)) {
        // 驱动对象创建失败，记录错误日志，并在返回前清理 WPP 追踪资源，
        // 否则会导致追踪系统的内存泄漏
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDriverCreate failed %!STATUS!", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

NTSTATUS
KMDriverEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,      // WDF 驱动对象句柄（在 DriverEntry 中创建）
    _Inout_ PWDFDEVICE_INIT DeviceInit   // 设备初始化结构体指针，用于在创建设备前配置各种参数
    )
/*++

函数说明：

    KMDriverEvtDeviceAdd 是"设备添加"事件回调函数，由 WDF 框架在响应
    PnP 管理器的 AddDevice 请求时调用。每当系统识别到一个新的匹配设备实例，
    就会调用一次本函数，驱动需要在这里完成以下工作：
    - 为该设备实例创建 WDF 设备对象
    - 初始化设备上下文（私有数据区域）
    - 设置设备接口、I/O 队列等

    本函数仅是一个转发包装，实际的设备创建逻辑委托给 Device.c 中的
    KMDriverCreateDevice 函数完成，以保持代码模块化。

参数说明：

    Driver     - WDF 驱动对象句柄，通常不需要在此直接使用
    DeviceInit - 不透明的设备初始化结构体，驱动可通过专用 API 对其进行配置。
                 WdfDeviceCreate 成功后，框架会自动释放该结构体，
                 因此之后不能再访问它。

返回值：

    NTSTATUS - 设备初始化结果状态码。失败时 PnP 管理器将中止设备加载流程。

--*/
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);  // 本函数不直接使用 Driver 参数，消除编译器警告

    PAGED_CODE();  // 断言当前 IRQL <= APC_LEVEL，确保本函数可以安全地被换出到分页内存

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    // 将设备创建工作委托给 Device.c 中的专用函数，
    // 该函数负责创建设备对象、初始化上下文、建立设备接口和 I/O 队列
    status = KMDriverCreateDevice(DeviceInit);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

VOID
KMDriverEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject  // WDF 驱动对象句柄（类型为 WDFOBJECT，兼容通用清理回调签名）
    )
/*++

函数说明：

    KMDriverEvtDriverContextCleanup 是驱动对象的清理回调函数，
    在以下情况下由 WDF 框架自动调用：
    - 驱动正常卸载（例如通过设备管理器禁用设备或调用 sc stop）
    - 驱动在加载过程中失败，框架进行回滚清理

    本函数的主要职责是停止 WPP 追踪会话，释放相关的追踪缓冲区和系统资源。
    如果 DriverEntry 中还分配了其他资源，也应在此一并释放。

参数说明：

    DriverObject - WDF 驱动对象句柄。需要通过 WdfDriverWdmGetDriverObject
                   将其转换为底层的 WDM DRIVER_OBJECT 指针，才能传给 WPP_CLEANUP。

返回值：

    无（VOID）

--*/
{
    UNREFERENCED_PARAMETER(DriverObject);  // 通过宏引用，消除编译器"未使用参数"警告

    PAGED_CODE();  // 断言当前运行在可分页代码允许的 IRQL 级别

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // 停止 WPP 追踪系统，释放所有追踪相关资源。
    // 必须将 WDFDRIVER 句柄转换回底层的 WDM DRIVER_OBJECT 指针，
    // 因为 WPP_CLEANUP 宏需要的是 WDM 层的驱动对象，而不是 WDF 包装对象。
    //
    WPP_CLEANUP( WdfDriverWdmGetDriverObject( (WDFDRIVER) DriverObject) );

}
