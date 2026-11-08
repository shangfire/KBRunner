/*++
模块名称：Queue.h
模块说明：声明顺序 IOCTL 队列初始化和命令处理回调，供 Device.c 注册使用。
运行环境：Windows 10/11 KMDF 内核模式，设备回调执行级别为 PASSIVE_LEVEL。
--*/
#ifndef _QUEUE_H_
#define _QUEUE_H_
// 初始化默认顺序队列；Device 为父设备，失败返回对应 NTSTATUS。
NTSTATUS KMDriverQueueInitialize(_In_ WDFDEVICE Device);
// 命令回调验证缓冲区并完成请求，错误状态直接返回用户态。
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL KMDriverEvtIoDeviceControl;
#endif
