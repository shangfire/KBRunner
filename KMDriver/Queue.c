/*++
模块名称：Queue.c
模块说明：校验用户态 IOCTL，更新设备状态并提交 VHF 输入，支持协议查询及全部释放。
运行环境：Windows 10/11 KMDF 内核模式，PASSIVE_LEVEL，顺序队列。
--*/
#include "driver.h"
#include "HIDReport.h"
#include "queue.tmh"

/* 函数说明：创建顺序默认队列。参数 Device 为父设备，返回 WdfIoQueueCreate 状态。
   每条命令在回调内完成，IoLock 保护与文件清理之间的共享状态。 */
NTSTATUS KMDriverQueueInitialize(_In_ WDFDEVICE Device)
{
    WDF_IO_QUEUE_CONFIG config; // 顺序队列配置，避免报文状态交错
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&config, WdfIoQueueDispatchSequential);
    config.EvtIoDeviceControl = KMDriverEvtIoDeviceControl;
    return WdfIoQueueCreate(Device, &config, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}

/* 函数说明：切换鼠标集合前释放旧集合的按钮，防止旧集合遗留按下状态。
   参数 Context 为设备状态，Absolute 为新模式；调用者持有 IoLock。
   返回值：无需切换则成功，否则返回旧模式释放报文的提交结果。 */
static NTSTATUS PrepareMouseMode(_In_ PDEVICE_CONTEXT Context, _In_ BOOLEAN Absolute)
{
    ULONG size = 0;        // 旧模式报文长度
    UCHAR report[8] = {0}; // 局部释放报文，避免覆盖当前状态
    if (Context->AbsoluteMode == Absolute) return STATUS_SUCCESS;
    if (Context->AbsoluteMode) BuildAbsMouseReport(Context->AbsoluteX, Context->AbsoluteY, 0, report, &size);
    else BuildMouseReport(0, 0, 0, report, &size);
    return KMDriverSubmit(Context, Context->AbsoluteMode ? 3 : 2, report, size);
}

/* 函数说明：校验并执行用户态命令，只有 VHF 接受报文后才报告成功。
   参数 Queue 为队列，Request 为请求，两个长度为缓冲区字节数，IoControlCode 为操作码。
   返回值：无；所有路径恰好完成请求一次，错误通过 NTSTATUS 返回用户态。
   等待锁只在 PASSIVE_LEVEL 获取；METHOD_BUFFERED 隔离用户态指针。 */
VOID KMDriverEvtIoDeviceControl(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength, _In_ size_t InputBufferLength, _In_ ULONG IoControlCode)
{
    PDEVICE_CONTEXT context = DeviceGetContext(WdfIoQueueGetDevice(Queue)); // 私有输入状态
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST; // 默认拒绝未知控制码
    PVOID input = NULL;                              // WDF 系统输入缓冲区
    ULONG_PTR information = 0;                      // 只有协议查询返回数据
    ULONG size = 0;                                 // 当前 HID 载荷长度
    UNREFERENCED_PARAMETER(OutputBufferLength);
    if (InputBufferLength != 0) {
        status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &input, NULL);
        if (!NT_SUCCESS(status)) { WdfRequestComplete(Request, status); return; }
    }
    WdfWaitLockAcquire(context->IoLock, NULL);
    status = STATUS_INVALID_DEVICE_REQUEST;
    if (context->Vhf == NULL) { status = STATUS_DEVICE_NOT_READY; goto Complete; }
    switch (IoControlCode) {
    case IOCTL_KM_QUERY_STATUS:
    {
        PULONG version = NULL; // 输出协议版本，防止错误连接过期驱动
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), (PVOID*)&version, NULL);
        if (NT_SUCCESS(status)) { *version = 1; information = sizeof(ULONG); }
        break;
    }
    case IOCTL_KM_RELEASE_ALL:
        status = KMDriverReleaseInputs(context);
        break;
    case IOCTL_HID_SEND_KEYBOARD_INPUT:
    {
        PKEYBOARD_INPUT_CMD command; // 键盘事件
        UCHAR candidate[8];         // 提交成功后再更新持久状态
        UCHAR hidCode;              // 普通键 HID 使用码
        UCHAR modifier;             // 修饰键位掩码
        BOOLEAN exists = FALSE;    // 防止重复按下占据多个槽位
        BOOLEAN available = FALSE; // 是否还可容纳普通键
        ULONG i;                   // 当前键槽索引
        if (InputBufferLength != sizeof(KEYBOARD_INPUT_CMD)) { status = STATUS_INFO_LENGTH_MISMATCH; break; }
        command = (PKEYBOARD_INPUT_CMD)input;
        hidCode = VirtualKeyToHidCode(command->KeyCode);
        modifier = GetModifierBits(command->KeyCode);
        if (command->KeyDown > 1 || (hidCode == 0 && modifier == 0)) { status = STATUS_INVALID_PARAMETER; break; }
        RtlCopyMemory(candidate, context->KeyboardReportBuffer, sizeof(candidate));
        for (i = 2; i < 8; ++i) { if (candidate[i] == hidCode) exists = TRUE; if (candidate[i] == 0) available = TRUE; }
        if (command->KeyDown && modifier == 0 && !exists && !available) { status = STATUS_BUFFER_OVERFLOW; break; }
        if (!(command->KeyDown && modifier == 0 && exists)) BuildKeyboardReport(command->KeyCode, command->KeyDown, candidate, &size);
        status = KMDriverSubmit(context, 1, candidate, sizeof(candidate));
        if (NT_SUCCESS(status)) RtlCopyMemory(context->KeyboardReportBuffer, candidate, sizeof(candidate));
        break;
    }
    case IOCTL_HID_SEND_MOUSE_INPUT:
    {
        PMOUSE_INPUT_CMD command; // 相对位移与按钮状态
        if (InputBufferLength != sizeof(MOUSE_INPUT_CMD)) { status = STATUS_INFO_LENGTH_MISMATCH; break; }
        command = (PMOUSE_INPUT_CMD)input;
        if (command->ButtonState > 7 || command->RelativeX < -127 || command->RelativeX > 127 || command->RelativeY < -127 || command->RelativeY > 127) {
            status = STATUS_INVALID_PARAMETER; break;
        }
        status = PrepareMouseMode(context, FALSE);
        if (!NT_SUCCESS(status)) break;
        BuildMouseReport(command->RelativeX, command->RelativeY, command->ButtonState, context->MouseReportBuffer, &size);
        status = KMDriverSubmit(context, 2, context->MouseReportBuffer, size);
        if (NT_SUCCESS(status)) context->AbsoluteMode = FALSE;
        break;
    }
    case IOCTL_HID_SEND_ABS_MOUSE_INPUT:
    {
        PABS_MOUSE_INPUT_CMD command; // 绝对坐标由 USHORT 保证在 0..65535 范围内
        if (InputBufferLength != sizeof(ABS_MOUSE_INPUT_CMD)) { status = STATUS_INFO_LENGTH_MISMATCH; break; }
        command = (PABS_MOUSE_INPUT_CMD)input;
        if (command->ButtonState > 7) { status = STATUS_INVALID_PARAMETER; break; }
        status = PrepareMouseMode(context, TRUE);
        if (!NT_SUCCESS(status)) break;
        BuildAbsMouseReport(command->AbsoluteX, command->AbsoluteY, command->ButtonState, context->MouseReportBuffer, &size);
        status = KMDriverSubmit(context, 3, context->MouseReportBuffer, size);
        if (NT_SUCCESS(status)) { context->AbsoluteMode = TRUE; context->AbsoluteX = command->AbsoluteX; context->AbsoluteY = command->AbsoluteY; }
        break;
    }
    default: break;
    }
Complete:
    WdfWaitLockRelease(context->IoLock);
    WdfRequestCompleteWithInformation(Request, status, information);
}
