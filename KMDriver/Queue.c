/*++

模块名称：

    queue.c

模块说明：

    本文件实现 I/O 队列的初始化和所有 I/O 请求的处理逻辑。

    核心函数 KMDriverEvtIoDeviceControl 是本驱动的"指令中枢"：
    用户态程序（如 Lua 脚本、自动化工具）通过 DeviceIoControl API 发送
    键盘/鼠标模拟命令，该函数根据 IOCTL 控制码将命令分发到对应的处理分支，
    调用 HIDReport.c 中的函数将命令编码为标准 HID 报文格式。

运行环境：

    内核模式驱动框架（Kernel-Mode Driver Framework，KMDF）

--*/

#include "driver.h"
#include "device.h"
#include "public.h"
#include "hidreport.h"   // HID 报文构建函数（BuildKeyboardReport、BuildMouseReport 等）
// queue.tmh 由 WPP 预处理器自动生成，提供本文件中 TraceEvents 所需的追踪元数据
#include "queue.tmh"

#ifdef ALLOC_PRAGMA
// KMDriverQueueInitialize 只在设备初始化阶段调用一次，放入可回收的 PAGE 段
#pragma alloc_text (PAGE, KMDriverQueueInitialize)
#endif

NTSTATUS
KMDriverQueueInitialize(
    _In_ WDFDEVICE Device  // WDF 设备对象句柄，新建的队列将归属于此设备
    )
/*++

函数说明：

    KMDriverQueueInitialize 负责创建和配置驱动的 I/O 请求队列。

    WDF 框架中的 I/O 队列有三种分发模式：
    - Sequential（顺序）：一次只处理一个请求，前一个完成后才处理下一个，最简单但并发低
    - Parallel（并行）：所有请求同时分发给回调函数，驱动负责自己做同步，并发最高
    - Manual（手动）：框架不自动分发，驱动主动从队列中取请求，灵活但复杂

    本驱动使用并行模式（WdfIoQueueDispatchParallel），因为 IOCTL 处理逻辑
    相互独立，不需要串行化，并行处理可以获得更好的响应性。

参数说明：

    Device - WDF 设备对象句柄，创建的队列会自动成为该设备的子对象，
             设备销毁时队列也会被自动销毁。

返回值：

    STATUS_SUCCESS        - 队列创建成功
    其他 NTSTATUS 错误码  - WdfIoQueueCreate 失败

--*/
{
    WDFQUEUE queue;                        // 创建成功后的 WDF 队列对象句柄
    NTSTATUS status;                       // 操作结果状态码
    WDF_IO_QUEUE_CONFIG queueConfig;       // 队列配置结构体

    PAGED_CODE();  // 运行时断言：确保当前 IRQL <= APC_LEVEL

    //
    // 初始化默认 I/O 队列配置。
    //
    // WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE 将此队列设置为"默认队列"：
    // 所有没有被 WdfDeviceConfigureRequestDispatching 明确路由到其他队列的请求，
    // 都会进入这个默认队列。
    //
    // 第二个参数指定分发模式为"并行"（WdfIoQueueDispatchParallel）：
    // 多个请求可以同时被分发到回调函数，适合处理相互独立的命令。
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
         &queueConfig,
        WdfIoQueueDispatchParallel
        );

    //
    // 注册 IOCTL 处理回调函数（EvtIoDeviceControl）：
    // 当用户态调用 DeviceIoControl 时，框架将请求放入队列后调用此回调。
    //
    queueConfig.EvtIoDeviceControl = KMDriverEvtIoDeviceControl;

    //
    // 注册队列停止回调函数（EvtIoStop）：
    // 当设备即将进入低功耗状态时，框架调用此回调通知驱动处理未完成的请求。
    //
    queueConfig.EvtIoStop = KMDriverEvtIoStop;

    //
    // 创建 WDF I/O 队列对象。
    //
    // 参数说明：
    //   Device              - 队列归属的设备对象，队列成为设备的子对象
    //   &queueConfig        - 已配置好的队列参数（分发模式、回调函数等）
    //   WDF_NO_OBJECT_ATTRIBUTES - 使用默认的对象属性（不需要自定义上下文或清理回调）
    //   &queue              - 输出参数，接收新建队列的句柄
    //
    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &queue
                 );

    if( !NT_SUCCESS(status) ) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        return status;
    }

    return status;
}

VOID
KMDriverEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,            // 触发本回调的 WDF 队列对象句柄
    _In_ WDFREQUEST Request,        // 当前正在处理的 I/O 请求对象句柄
    _In_ size_t OutputBufferLength, // 用户态提供的输出缓冲区大小（字节），本驱动通常不需要输出数据
    _In_ size_t InputBufferLength,  // 用户态提供的输入缓冲区大小（字节），包含命令数据
    _In_ ULONG IoControlCode        // IOCTL 控制码，标识用户态请求执行哪种操作
    )
/*++

函数说明：

    KMDriverEvtIoDeviceControl 是本驱动处理 DeviceIoControl 请求的核心回调。
    每当用户态应用程序调用 DeviceIoControl(hDevice, IOCTL_xxx, ...) 时，
    操作系统将请求转化为 IRP_MJ_DEVICE_CONTROL，WDF 框架再调用本函数。

    处理流程：
    1. 从队列获取设备对象，进而获取设备上下文（DEVICE_CONTEXT）
    2. 如果有输入数据，从请求中取出输入缓冲区指针
    3. 如果需要输出数据，从请求中取出输出缓冲区指针
    4. 根据 IoControlCode 分发到对应的处理分支
    5. 调用 HIDReport.c 的函数构建 HID 报文
    6. 调用 WdfRequestCompleteWithInformation 完成请求，通知用户态操作已完成

    当前支持的三种 IOCTL：
    - IOCTL_HID_SEND_KEYBOARD_INPUT：模拟键盘按键事件
    - IOCTL_HID_SEND_MOUSE_INPUT：模拟相对鼠标移动
    - IOCTL_HID_SEND_ABS_MOUSE_INPUT：模拟绝对鼠标定位

参数说明：

    Queue             - I/O 队列句柄，通过它可以获取关联的设备对象
    Request           - 当前请求对象，包含输入/输出缓冲区信息，必须在函数返回前完成（Complete）
    OutputBufferLength - 用户态输出缓冲区大小，本驱动命令处理不需要返回数据，通常为 0
    InputBufferLength  - 用户态输入缓冲区大小，用于验证数据完整性
    IoControlCode      - IOCTL 控制码，决定执行哪种输入模拟操作

返回值：

    无（VOID）。所有路径必须调用 WdfRequestComplete 或 WdfRequestCompleteWithInformation
    完成请求，否则请求会一直挂起，用户态 DeviceIoControl 调用将永久阻塞。

--*/
{
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;  // 默认状态：不支持的控制码
    PDEVICE_CONTEXT deviceContext;                    // 设备私有上下文数据指针
    WDFDEVICE device;                                 // 设备对象句柄
    PVOID inputBuffer = NULL;                         // 指向用户态输入缓冲区的内核指针
    PVOID outputBuffer = NULL;                        // 指向用户态输出缓冲区的内核指针（本驱动通常不用）
    size_t bytesTransferred = 0;                      // 实际传输/处理的字节数，随请求完成一起返回给用户态

    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "%!FUNC! Queue 0x%p, Request 0x%p IOCTL 0x%X", 
                Queue, Request, IoControlCode);

    //
    // 从队列对象获取关联的设备对象句柄，
    // 然后通过设备上下文访问器获取本设备的私有数据
    //
    device = WdfIoQueueGetDevice(Queue);
    deviceContext = DeviceGetContext(device);

    //
    // 获取输入缓冲区（如果用户态提供了输入数据）。
    //
    // 由于队列使用 METHOD_BUFFERED 方式，系统已将用户态缓冲区的数据
    // 拷贝到内核分配的系统缓冲区中，WdfRequestRetrieveInputBuffer 返回
    // 指向该系统缓冲区的安全指针，可以直接在内核中访问，无需担心用户态地址验证。
    //
    if (InputBufferLength > 0) {
        status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &inputBuffer, NULL);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
                        "WdfRequestRetrieveInputBuffer failed %!STATUS!", status);
            WdfRequestComplete(Request, status);  // 必须完成请求，否则用户态将永久阻塞
            return;
        }
    }

    //
    // 获取输出缓冲区（如果用户态提供了输出空间）。
    // 对于命令型 IOCTL，通常不需要向用户态返回数据，OutputBufferLength 为 0。
    //
    if (OutputBufferLength > 0) {
        status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, &outputBuffer, NULL);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
                        "WdfRequestRetrieveOutputBuffer failed %!STATUS!", status);
            WdfRequestComplete(Request, status);
            return;
        }
    }

    //
    // 根据 IOCTL 控制码分发到对应的处理逻辑
    //
    switch (IoControlCode) {

        case IOCTL_HID_SEND_KEYBOARD_INPUT:
        {
            // 定义局部变量：指向键盘命令结构体的指针
            PKEYBOARD_INPUT_CMD keyCmd;

            // 验证输入缓冲区大小是否满足命令结构体的最小要求，
            // 防止越界访问（缓冲区溢出攻击防御）
            if (InputBufferLength < sizeof(KEYBOARD_INPUT_CMD)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            // 将通用 PVOID 指针强转为具体的命令结构体指针
            keyCmd = (PKEYBOARD_INPUT_CMD)inputBuffer;

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE,
                        "Keyboard IOCTL: KeyCode=0x%X, KeyDown=%d",
                        keyCmd->KeyCode, keyCmd->KeyDown);

            //
            // 调用 HIDReport.c 中的函数，将 Windows 虚拟键码转换为 HID 键码，
            // 并更新设备上下文中的 KeyboardReportBuffer（8 字节 HID 报文）。
            //
            // TODO（待实现）：构建报文后，需要通过 HID 子系统的 ReadReport 机制
            // 将报文注入到操作系统的 HID 输入流，才能真正让系统感知到按键事件。
            //
            ULONG reportSize = 0;
            BuildKeyboardReport(keyCmd->KeyCode, keyCmd->KeyDown,
                               deviceContext->KeyboardReportBuffer,
                               &reportSize);

            status = STATUS_SUCCESS;
            bytesTransferred = 0;  // 本 IOCTL 不返回数据给用户态
            break;
        }

        case IOCTL_HID_SEND_MOUSE_INPUT:
        {
            // 定义局部变量：指向相对鼠标命令结构体的指针
            PMOUSE_INPUT_CMD mouseCmd;

            // 验证输入缓冲区大小，防止结构体字段访问越界
            if (InputBufferLength < sizeof(MOUSE_INPUT_CMD)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            mouseCmd = (PMOUSE_INPUT_CMD)inputBuffer;

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE,
                        "Relative Mouse IOCTL: X=%d, Y=%d, Buttons=0x%X",
                        mouseCmd->RelativeX, mouseCmd->RelativeY, mouseCmd->ButtonState);

            //
            // 构建相对鼠标 HID 报文（4 字节格式）：
            // [按键位掩码][X偏移量（有符号字节）][Y偏移量（有符号字节）][滚轮（可选）]
            //
            // 注意：HID 相对鼠标的单轴偏移量通常为有符号字节（-128 ~ +127），
            // BuildMouseReport 内部会对超出范围的值进行截断（Clamping）。
            //
            ULONG reportSize = 0;
            BuildMouseReport(mouseCmd->RelativeX, mouseCmd->RelativeY,
                            mouseCmd->ButtonState,
                            deviceContext->MouseReportBuffer,
                            &reportSize);

            status = STATUS_SUCCESS;
            bytesTransferred = 0;
            break;
        }

        case IOCTL_HID_SEND_ABS_MOUSE_INPUT:
        {
            // 定义局部变量：指向绝对鼠标命令结构体的指针
            PABS_MOUSE_INPUT_CMD absMouseCmd;

            // 验证输入缓冲区大小
            if (InputBufferLength < sizeof(ABS_MOUSE_INPUT_CMD)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            absMouseCmd = (PABS_MOUSE_INPUT_CMD)inputBuffer;

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE,
                        "Absolute Mouse IOCTL: X=%u, Y=%u, Buttons=0x%X",
                        absMouseCmd->AbsoluteX, absMouseCmd->AbsoluteY, absMouseCmd->ButtonState);

            //
            // 构建绝对鼠标 HID 报文（5 字节格式）：
            // [按键位掩码][X低字节][X高字节][Y低字节][Y高字节]
            //
            // 绝对坐标范围 0~65535 会被 HID 子系统线性映射到屏幕实际分辨率，
            // 例如在 1920x1080 的屏幕上：X=32768 大约对应屏幕中央水平位置。
            //
            ULONG reportSize = 0;
            BuildAbsMouseReport(absMouseCmd->AbsoluteX, absMouseCmd->AbsoluteY,
                               absMouseCmd->ButtonState,
                               deviceContext->MouseReportBuffer,
                               &reportSize);

            status = STATUS_SUCCESS;
            bytesTransferred = 0;
            break;
        }

        default:
            // 未知的 IOCTL 控制码，返回"不支持此设备请求"错误
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    //
    // 完成 I/O 请求。
    //
    // WdfRequestCompleteWithInformation 做两件事：
    // 1. 将 status 作为操作结果状态码返回给用户态（DeviceIoControl 的返回值）
    // 2. 将 bytesTransferred 填入 IRP 的 Information 字段，
    //    用户态可通过 DeviceIoControl 的最后一个参数（lpBytesReturned）读取
    //
    // 注意：每个请求有且仅能被完成一次，重复调用会导致系统崩溃（Bug Check）
    //
    WdfRequestCompleteWithInformation(Request, status, bytesTransferred);

    return;
}

VOID
KMDriverEvtIoStop(
    _In_ WDFQUEUE Queue,      // 触发本回调的 WDF 队列对象句柄
    _In_ WDFREQUEST Request,  // 正在被处理或挂起的 I/O 请求对象
    _In_ ULONG ActionFlags    // 停止原因标志位，由 WDF_REQUEST_STOP_ACTION_FLAGS 枚举定义
)
/*++

函数说明：

    KMDriverEvtIoStop 是电源管理回调，当设备即将离开工作状态（D0）时，
    WDF 框架对队列中每个仍在处理的请求调用一次本回调。

    驱动必须在本回调中做出决定（通常选择以下之一）：
    A. 立即完成请求（WdfRequestComplete）：如果可以安全地中断处理
    B. 取消请求（WdfRequestCancelSentRequest）：如果请求已转发给 I/O Target
    C. 暂停并确认（WdfRequestStopAcknowledge）：通知框架驱动知道要停止，
       稍后会主动完成该请求（适合需要等待硬件操作完成的场景）

    对于本驱动中处理时间极短的 IOCTL 命令，选择不做任何操作是合理的，
    因为这些请求会在极短时间内自然完成，不会阻止设备进入低功耗状态。

参数说明：

    Queue       - 队列对象句柄
    Request     - 待处理的请求对象
    ActionFlags - 停止原因和请求属性的组合标志，包括：
                  WdfRequestStopActionSuspend：设备进入低功耗状态（如睡眠）
                  WdfRequestStopActionPurge：设备被移除或禁用
                  WdfRequestStopRequestCancelable：请求当前可被取消

返回值：

    无（VOID）

--*/
{
    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "%!FUNC! Queue 0x%p, Request 0x%p ActionFlags %d", 
                Queue, Request, ActionFlags);

    //
    // 本驱动选择在 EvtIoStop 中不做任何操作。
    //
    // 理由：
    // 1. 本驱动处理的 IOCTL 命令（键盘/鼠标输入）在单次调用中即可完成，
    //    不存在长时间挂起的请求，因此无需特殊处理
    // 2. 如果将来驱动有长时间异步操作（如等待硬件中断），则需要在此
    //    调用 WdfRequestStopAcknowledge(Request, FALSE) 暂停处理，
    //    或调用 WdfRequestComplete 提前终止请求
    //
    return;
}
