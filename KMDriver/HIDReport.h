/*++

模块名称：

    hidreport.h

模块说明：

    本文件声明了 HID 报文构建函数的接口。

    HID（Human Interface Device，人机接口设备）协议规定了输入设备向操作系统
    报告输入事件的标准数据格式。驱动程序必须按照 HID 报告描述符中定义的格式，
    将输入状态打包成字节数组（"Report"，报文），提交给操作系统的 HID 类驱动，
    系统再将其转化为应用程序可以接收的消息（如 WM_KEYDOWN、WM_MOUSEMOVE 等）。

    本文件中的三个函数分别负责：
    1. BuildKeyboardReport - 将 Windows 虚拟键码转换为标准 8 字节 HID 键盘报文
    2. BuildMouseReport    - 将相对移动量和按键状态编码为 4 字节 HID 鼠标报文
    3. BuildAbsMouseReport - 将绝对坐标和按键状态编码为 5 字节 HID 绝对鼠标报文

运行环境：

    内核模式驱动框架（Kernel-Mode Driver Framework，KMDF）

--*/

#ifndef _HIDREPORT_H_
#define _HIDREPORT_H_

EXTERN_C_START

//
// 构建 HID 键盘报文
//
// 将一次按键事件（按下或松开）编码进 8 字节的标准 USB HID 键盘报文缓冲区。
// 函数内部维护按键状态的增量更新：
//   - 按下时：将键码加入报文中的空闲槽位，或设置修饰键位
//   - 松开时：从报文中清除对应的键码，或清除修饰键位
//
// 参数：
//   KeyCode      - Windows 虚拟键码（VK_xxx 常量，如 VK_RETURN=0x0D）
//   KeyDown      - TRUE 表示按下，FALSE 表示松开
//   ReportBuffer - 指向 8 字节 HID 键盘报文缓冲区的指针，函数会修改此缓冲区
//   ReportSize   - 输出参数，函数将报文有效字节数（固定为 8）写入此处
//
VOID BuildKeyboardReport(
    _In_ UCHAR KeyCode,
    _In_ BOOLEAN KeyDown,
    _Out_ PUCHAR ReportBuffer,
    _Inout_ PULONG ReportSize
);

//
// 构建 HID 相对鼠标报文
//
// 将一次相对鼠标移动事件编码为 4 字节的 HID 鼠标报文。
// "相对移动"是指相对于鼠标当前位置的偏移量，模拟普通物理鼠标的移动方式。
//
// 参数：
//   RelativeX    - X 轴相对位移（有符号，正值向右，负值向左）
//                  函数内部会将超出 -128~+127 范围的值截断到该范围
//   RelativeY    - Y 轴相对位移（有符号，正值向下，负值向上）
//   ButtonState  - 按键状态位掩码：bit 0=左键，bit 1=右键，bit 2=中键
//   ReportBuffer - 指向鼠标报文缓冲区的指针，函数会完整重写此缓冲区
//   ReportSize   - 输出参数，函数将报文有效字节数（固定为 4）写入此处
//
VOID BuildMouseReport(
    _In_ SHORT RelativeX,
    _In_ SHORT RelativeY,
    _In_ USHORT ButtonState,
    _Out_ PUCHAR ReportBuffer,
    _Inout_ PULONG ReportSize
);

//
// 构建 HID 绝对鼠标报文
//
// 将一次绝对坐标定位事件编码为 5 字节的 HID 绝对鼠标报文。
// "绝对坐标"表示屏幕上的固定位置，范围 0~65535 由 HID 子系统映射到实际分辨率。
// 适用于需要精确点击特定像素位置的场景（游戏辅助、自动化测试等）。
//
// 参数：
//   AbsoluteX    - X 轴绝对坐标（0=最左，65535=最右）
//   AbsoluteY    - Y 轴绝对坐标（0=最上，65535=最下）
//   ButtonState  - 按键状态位掩码（同 BuildMouseReport）
//   ReportBuffer - 指向鼠标报文缓冲区的指针，函数会完整重写此缓冲区
//   ReportSize   - 输出参数，函数将报文有效字节数（固定为 5）写入此处
//
VOID BuildAbsMouseReport(
    _In_ USHORT AbsoluteX,
    _In_ USHORT AbsoluteY,
    _In_ USHORT ButtonState,
    _Out_ PUCHAR ReportBuffer,
    _Inout_ PULONG ReportSize
);

EXTERN_C_END

#endif // _HIDREPORT_H_
