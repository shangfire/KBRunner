KMDriver
========
Windows 10/11 KMDF + VHF 虚拟键盘鼠标源驱动。

Device.c/h：创建独占命令设备和 VHF 子设备，管理输入释放及 PnP 生命周期。
Queue.c/h：校验、分发并执行 IOCTL，协议版本查询及释放全部输入。
HIDReport.c/h：虚拟键码到 HID 使用码映射、键盘与鼠标载荷编码。
Public.h：用户态/内核态控制码与命令布局。
Shared.h：早期共享内存方案的历史定义，当前实现不使用。
Driver.c/h、Trace.h：驱动入口、WDF 生命周期与 WPP 日志。
KMDriver.inf：Root\KMDriver 的安装配置，注册 vhf 为 LowerFilters。

安装需要创建 root 枚举设备，不能仅通过 sc create/start 创建服务。
完整使用说明和构建方式见根目录 README.md。
