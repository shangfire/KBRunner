/*++

模块名称：

    queue.h

模块说明：

    本文件定义了 I/O 队列相关的数据结构和函数/回调声明。

    I/O 队列（I/O Queue）是 WDF 框架中处理来自用户态的 I/O 请求的核心机制。
    用户态程序调用 DeviceIoControl、ReadFile、WriteFile 等 API 时，
    操作系统将其转化为 IRP（I/O Request Packet），WDF 框架再将 IRP 包装成
    WDFREQUEST 对象，放入对应的队列，然后调用驱动注册的回调函数处理。

    本驱动使用一个默认的并行分发队列（WdfIoQueueDispatchParallel），
    接收来自用户态的三种 IOCTL 控制命令：
    - IOCTL_HID_SEND_KEYBOARD_INPUT（键盘输入）
    - IOCTL_HID_SEND_MOUSE_INPUT（相对鼠标移动）
    - IOCTL_HID_SEND_ABS_MOUSE_INPUT（绝对鼠标定位）

运行环境：

    内核模式驱动框架（Kernel-Mode Driver Framework，KMDF）

--*/

EXTERN_C_START

//
// 队列上下文结构体（Queue Context）
//
// 与设备上下文类似，队列上下文是与 WDF 队列对象关联的私有数据区域。
// 当前本驱动的队列不需要存储任何私有状态，PrivateDeviceData 仅作占位符，
// 保持结构体非空（C 语言规范要求结构体至少有一个成员）。
//
typedef struct _QUEUE_CONTEXT {
    ULONG PrivateDeviceData;  // 占位成员，当前未使用。
                              // 如果将来需要为队列存储状态（例如待处理请求数量、
                              // 超时计时器句柄等），可以在此添加字段。
} QUEUE_CONTEXT, *PQUEUE_CONTEXT;

// 生成队列上下文的类型安全访问函数 QueueGetContext(queue)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CONTEXT, QueueGetContext)

//
// 队列初始化函数声明
//
// 在 KMDriverCreateDevice 的最后一步调用，
// 负责创建 I/O 队列并注册 IOCTL 处理回调函数。
//
NTSTATUS
KMDriverQueueInitialize(
    _In_ WDFDEVICE Device  // WDF 设备对象句柄，队列将从属于该设备
    );

//
// I/O 队列事件回调函数声明
//
// EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL：
//   当用户态调用 DeviceIoControl 时触发，是本驱动的核心处理函数。
//   所有的键盘/鼠标模拟命令都通过这个回调进入内核处理。
//
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL KMDriverEvtIoDeviceControl;

//
// EVT_WDF_IO_QUEUE_IO_STOP：
//   当设备即将离开工作状态（D0）时，框架通知驱动停止处理队列中的请求。
//   驱动需要决定：是立即完成/取消正在处理的请求，还是暂停并等待后续恢复。
//
EVT_WDF_IO_QUEUE_IO_STOP KMDriverEvtIoStop;

EXTERN_C_END
