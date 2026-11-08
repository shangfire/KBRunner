/*++
模块名称：Device.h
模块说明：定义 VHF 虚拟键鼠的设备状态和生命周期回调，与 Queue.c 共用。
运行环境：Windows 10/11，KMDF 内核模式，状态访问由 IoLock 串行保护。
--*/
#ifndef _DEVICE_H_
#define _DEVICE_H_
#include <vhf.h>
#include "public.h"
typedef struct _DEVICE_CONTEXT {
    VHFHANDLE Vhf;                 // VHF 实例，硬件准备时创建，释放硬件时同步销毁
    WDFWAITLOCK IoLock;             // 等待锁，所有访问均在 PASSIVE_LEVEL，禁止提升 IRQL 后获取
    UCHAR KeyboardReportBuffer[8];  // 持久化键盘状态，包含修饰键与最多六个普通键
    UCHAR MouseReportBuffer[8];     // 当前鼠标报文暂存区
    USHORT AbsoluteX;              // 最近一次绝对横坐标，释放按钮时保持指针位置
    USHORT AbsoluteY;              // 最近一次绝对纵坐标
    BOOLEAN AbsoluteMode;          // 最近提交是否为绝对鼠标，清理时用于选择正确报文
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)
NTSTATUS KMDriverCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit);
EVT_WDF_DEVICE_PREPARE_HARDWARE KMDriverEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE KMDriverEvtDeviceReleaseHardware;
EVT_WDF_FILE_CLEANUP KMDriverEvtFileCleanup;
NTSTATUS KMDriverSubmit(_In_ PDEVICE_CONTEXT Context, _In_ UCHAR Id, _In_reads_bytes_(Size) PUCHAR Data, _In_ ULONG Size);
NTSTATUS KMDriverReleaseInputs(_In_ PDEVICE_CONTEXT Context);
#endif
