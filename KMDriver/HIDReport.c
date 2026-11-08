/*++

模块名称：

    hidreport.c

模块说明：

    本文件实现 HID 报文的编码逻辑，是本驱动与 HID 协议规范之间的转换层。

    背景知识：
    HID（Human Interface Device）是 USB 协议的一个子类，定义了输入设备
    （键盘、鼠标、游戏手柄等）与主机之间交换数据的标准格式。
    每次输入事件被编码为一个固定格式的字节数组，称为"报文"（Report）。
    报文格式由 HID 报告描述符（Report Descriptor）定义，内核驱动必须严格
    遵循描述符中规定的格式，否则系统会忽略或错误解释输入数据。

    本文件包含：
    1. VirtualKeyToHidCode   - Windows 虚拟键码 → HID 使用码的转换表
    2. GetModifierBits       - 判断某个键是否是修饰键，并返回对应的位掩码
    3. BuildKeyboardReport   - 构建 8 字节键盘 HID 报文
    4. BuildMouseReport      - 构建 4 字节相对鼠标 HID 报文
    5. BuildAbsMouseReport   - 构建 5 字节绝对鼠标 HID 报文

运行环境：

    内核模式驱动框架（Kernel-Mode Driver Framework，KMDF）

--*/

#include "driver.h"

//
// ===========================================================================
// HID 键盘报文格式说明（来自 USB HID Usage Tables 规范 v1.12）
// ===========================================================================
//
// 标准 USB 键盘 HID 报文为 8 字节：
//
//   Byte 0：修饰键位掩码（Modifier Keys Bitmap）
//           ┌────┬────┬────┬──────────┬──────────┬──────────┬──────────┬──────────┐
//           │bit7│bit6│bit5│  bit4    │  bit3    │  bit2    │  bit1    │  bit0    │
//           │RGUI│RAlt│RSft│RCtrl(注意│LGUI(Win) │LAlt      │LShift    │LCtrl     │
//           └────┴────┴────┴──────────┴──────────┴──────────┴──────────┴──────────┘
//
//   Byte 1：保留字节，协议要求填充 0x00（OEM 保留，当前 HID 版本不使用）
//
//   Byte 2-7：按下键的 HID Usage ID（键码）
//             最多同时报告 6 个键（6-Key Rollover，简称 6KRO）
//             未使用的槽位填 0x00（表示无按键）
//             若同时按下超过 6 个键，报文中所有键码槽位填 0x01（Rollover 错误码）
//
// HID Usage ID 部分常用映射（来自 HID Usage Tables - Keyboard/Keypad Page 0x07）：
//   0x04 = 字母 A          0x27 = 数字 0           0x28 = 回车（Enter）
//   0x05-0x1D = 字母 B~Z   0x1E-0x26 = 数字 1~9   0x29 = ESC
//   0x2A = Backspace       0x2B = Tab              0x2C = 空格
//   0x3A-0x45 = F1~F12     0x4A = Home             0x4D = End
//
// ===========================================================================

//
// Windows 虚拟键码（VK_xxx）常量定义
//
// Windows API 中的虚拟键码（Virtual Key Code，VK）是一套与具体键盘布局无关的
// 抽象键码系统，定义在 winuser.h 中。由于内核头文件不包含 winuser.h，
// 这里手动定义本驱动所需的 VK 常量。
//
#define VK_LCONTROL   0xA2   // 左 Ctrl 键
#define VK_RCONTROL   0xA3   // 右 Ctrl 键
#define VK_LSHIFT     0xA0   // 左 Shift 键
#define VK_RSHIFT     0xA1   // 右 Shift 键
#define VK_LMENU      0xA4   // 左 Alt 键（MENU 是 Alt 的历史名称）
#define VK_RMENU      0xA5   // 右 Alt 键
#define VK_LWIN       0x5B   // 左 Windows（Win）键
#define VK_RWIN       0x5C   // 右 Windows（Win）键
#define VK_RETURN     0x0D   // 回车键（Enter）
#define VK_ESCAPE     0x1B   // 逃脱键（Esc）
#define VK_BACK       0x08   // 退格键（Backspace）
#define VK_TAB        0x09   // 制表键（Tab）
#define VK_SPACE      0x20   // 空格键（Space）
#define VK_OEM_MINUS  0xBD   // 减号/下划线键（- _）
#define VK_OEM_PLUS   0xBB   // 等号/加号键（= +）
#define VK_OEM_4      0xDB   // 左方括号键（[ {）
#define VK_OEM_6      0xDD   // 右方括号键（] }）
#define VK_OEM_5      0xDC   // 反斜杠/竖线键（\ |）
#define VK_OEM_1      0xBA   // 分号/冒号键（; :）
#define VK_OEM_7      0xDE   // 单引号/双引号键（' "）
#define VK_OEM_3      0xC0   // 波浪号/反引号键（` ~）
#define VK_OEM_COMMA  0xBC   // 逗号/小于号键（, <）
#define VK_OEM_PERIOD 0xBE   // 句号/大于号键（. >）
#define VK_OEM_2      0xBF   // 斜杠/问号键（/ ?）
#define VK_CAPITAL    0x14   // 大写锁定键（Caps Lock）
#define VK_F1         0x70   // F1 功能键
#define VK_F2         0x71   // F2 功能键
#define VK_F3         0x72   // F3 功能键
#define VK_F4         0x73   // F4 功能键
#define VK_F5         0x74   // F5 功能键
#define VK_F6         0x75   // F6 功能键
#define VK_F7         0x76   // F7 功能键
#define VK_F8         0x77   // F8 功能键
#define VK_F9         0x78   // F9 功能键
#define VK_F10        0x79   // F10 功能键
#define VK_F11        0x7A   // F11 功能键
#define VK_F12        0x7B   // F12 功能键
#define VK_HOME       0x24   // Home 键
#define VK_PRIOR      0x21   // Page Up 键（PRIOR = 前一页）
#define VK_DELETE     0x2E   // Delete 键
#define VK_END        0x23   // End 键
#define VK_NEXT       0x22   // Page Down 键（NEXT = 下一页）
#define VK_RIGHT      0x27   // 右方向键
#define VK_LEFT       0x25   // 左方向键
#define VK_DOWN       0x28   // 下方向键
#define VK_UP         0x26   // 上方向键

//
// VirtualKeyToHidCode - 将 Windows 虚拟键码转换为 HID Usage ID（键码）
//
// 背景：Windows 使用自己的虚拟键码系统（VK_xxx），而 HID 协议使用另一套
// "Usage ID"编码（来自 USB HID Usage Tables 规范的 Keyboard/Keypad 页面）。
// 两者之间没有线性关系，必须通过查表（映射）来转换。
//
// 本函数实现了最常用的映射子集，完整的 HID 键码映射表参见：
// https://usb.org/sites/default/files/hut1_4.pdf  第 10 章（Keyboard/Keypad Page）
//
UCHAR VirtualKeyToHidCode(UCHAR VirtualKey)
{
    //
    // 字母键映射：VK 码 'A'(0x41) ~ 'Z'(0x5A) 和 'a'(0x61) ~ 'z'(0x7A)
    // HID 键码中字母 A=0x04，B=0x05，...，Z=0x1D（连续排列，但与 ASCII 不对齐）
    // 注意：HID 键码不区分大小写，大小写由 Shift/CapsLock 修饰键决定
    //
    if (VirtualKey >= 'A' && VirtualKey <= 'Z') {
        return 0x04 + (VirtualKey - 'A');  // A→0x04, B→0x05, ..., Z→0x1D
    }
    if (VirtualKey >= 'a' && VirtualKey <= 'z') {
        return 0x04 + (VirtualKey - 'a');  // 小写字母与大写映射到相同的 HID 键码
    }

    //
    // 数字键映射：VK 码 '1'(0x31) ~ '9'(0x39)
    // HID 键码中数字 1=0x1E，2=0x1F，...，9=0x26（连续排列）
    //
    if (VirtualKey >= '1' && VirtualKey <= '9') {
        return 0x1E + (VirtualKey - '1');  // 1→0x1E, 2→0x1F, ..., 9→0x26
    }
    if (VirtualKey == '0') {
        return 0x27;  // 数字 0 在 HID 键码中不紧跟在 9 之后，需要单独处理
    }

    //
    // 特殊功能键映射（非连续，逐一枚举）
    //
    if (VirtualKey == VK_RETURN)     return 0x28;  // Enter 键
    if (VirtualKey == VK_ESCAPE)     return 0x29;  // Esc 键
    if (VirtualKey == VK_BACK)       return 0x2A;  // Backspace 键
    if (VirtualKey == VK_TAB)        return 0x2B;  // Tab 键
    if (VirtualKey == VK_SPACE)      return 0x2C;  // 空格键
    if (VirtualKey == VK_OEM_MINUS)  return 0x2D;  // 减号/下划线键（- _）
    if (VirtualKey == VK_OEM_PLUS)   return 0x2E;  // 等号/加号键（= +）
    if (VirtualKey == VK_OEM_4)      return 0x2F;  // 左方括号键（[ {）
    if (VirtualKey == VK_OEM_6)      return 0x30;  // 右方括号键（] }）
    if (VirtualKey == VK_OEM_5)      return 0x31;  // 反斜杠/竖线键（\ |）
    if (VirtualKey == VK_OEM_1)      return 0x33;  // 分号/冒号键（; :）
    if (VirtualKey == VK_OEM_7)      return 0x34;  // 单引号/双引号键（' "）
    if (VirtualKey == VK_OEM_3)      return 0x35;  // 波浪号/反引号键（` ~）
    if (VirtualKey == VK_OEM_COMMA)  return 0x36;  // 逗号/小于号键（, <）
    if (VirtualKey == VK_OEM_PERIOD) return 0x37;  // 句号/大于号键（. >）
    if (VirtualKey == VK_OEM_2)      return 0x38;  // 斜杠/问号键（/ ?）
    if (VirtualKey == VK_CAPITAL)    return 0x39;  // Caps Lock 大写锁定键

    //
    // 功能键映射：F1~F12 在 HID 中连续排列（0x3A ~ 0x45）
    //
    if (VirtualKey == VK_F1)  return 0x3A;  // F1
    if (VirtualKey == VK_F2)  return 0x3B;  // F2
    if (VirtualKey == VK_F3)  return 0x3C;  // F3
    if (VirtualKey == VK_F4)  return 0x3D;  // F4
    if (VirtualKey == VK_F5)  return 0x3E;  // F5
    if (VirtualKey == VK_F6)  return 0x3F;  // F6
    if (VirtualKey == VK_F7)  return 0x40;  // F7
    if (VirtualKey == VK_F8)  return 0x41;  // F8
    if (VirtualKey == VK_F9)  return 0x42;  // F9
    if (VirtualKey == VK_F10) return 0x43;  // F10
    if (VirtualKey == VK_F11) return 0x44;  // F11
    if (VirtualKey == VK_F12) return 0x45;  // F12

    //
    // 导航键映射（光标控制区）
    //
    if (VirtualKey == VK_HOME)   return 0x4A;  // Home 键（跳到行首/文档开头）
    if (VirtualKey == VK_PRIOR)  return 0x4B;  // Page Up 键（向上翻页）
    if (VirtualKey == VK_DELETE) return 0x4C;  // Delete 键（删除光标后的字符）
    if (VirtualKey == VK_END)    return 0x4D;  // End 键（跳到行尾/文档结尾）
    if (VirtualKey == VK_NEXT)   return 0x4E;  // Page Down 键（向下翻页）
    if (VirtualKey == VK_RIGHT)  return 0x4F;  // 右方向键
    if (VirtualKey == VK_LEFT)   return 0x50;  // 左方向键
    if (VirtualKey == VK_DOWN)   return 0x51;  // 下方向键
    if (VirtualKey == VK_UP)     return 0x52;  // 上方向键

    return 0x00;  // 未知键或不支持的键，返回 0（HID 协议中 0x00 表示"无按键"）
}

//
// GetModifierBits - 获取修饰键对应的 HID 键盘报文 Byte 0 位掩码
//
// HID 键盘报文的 Byte 0 专门用于修饰键（Modifier Keys），每个修饰键占一个 bit。
// 修饰键不占用 Byte 2-7 的键码槽位，而是通过 Byte 0 的位操作来设置/清除。
// 这是 HID 协议的设计规范，与普通键的处理方式完全不同。
//
// 若传入的键码是修饰键，返回对应的位掩码；否则返回 0x00。
//
UCHAR GetModifierBits(UCHAR VirtualKey)
{
    if (VirtualKey == VK_LCONTROL) return 0x01;  // 左 Ctrl   → bit 0
    if (VirtualKey == VK_LSHIFT)   return 0x02;  // 左 Shift  → bit 1
    if (VirtualKey == VK_LMENU)    return 0x04;  // 左 Alt    → bit 2
    if (VirtualKey == VK_LWIN)     return 0x08;  // 左 Win    → bit 3
    if (VirtualKey == VK_RCONTROL) return 0x10;  // 右 Ctrl   → bit 4
    if (VirtualKey == VK_RSHIFT)   return 0x20;  // 右 Shift  → bit 5
    if (VirtualKey == VK_RMENU)    return 0x40;  // 右 Alt    → bit 6
    if (VirtualKey == VK_RWIN)     return 0x80;  // 右 Win    → bit 7
    return 0x00;  // 不是修饰键，返回 0
}

//
// BuildKeyboardReport - 将按键事件更新到 8 字节 HID 键盘报文缓冲区
//
VOID BuildKeyboardReport(
    _In_ UCHAR KeyCode,         // Windows 虚拟键码
    _In_ BOOLEAN KeyDown,       // TRUE=按下，FALSE=松开
    _Out_ PUCHAR ReportBuffer,  // 指向 8 字节键盘报文缓冲区，函数会修改其内容
    _Inout_ PULONG ReportSize   // 输出参数，函数将报文字节数（8）写入此处
)
/*++

函数说明：

    BuildKeyboardReport 以"增量更新"方式维护 HID 键盘报文：
    - 调用方持续传入每次按键事件（按下/松开），函数相应地修改 ReportBuffer
    - ReportBuffer 始终保存着当前所有按下键的完整快照状态
    - 将 ReportBuffer 直接提交给 HID 子系统即可反映当前键盘状态

    这种设计允许多键同时按下（N-Key）：
    例如按住 Ctrl + Alt + Delete：
      1. 按下 VK_LCONTROL → Byte 0 |= 0x01
      2. 按下 VK_LMENU   → Byte 0 |= 0x04
      3. 按下 VK_DELETE  → Byte 2  = 0x4C
      此时报文为：[0x05, 0x00, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00]

    限制：最多同时报告 6 个非修饰键（Byte 2-7 共 6 个槽位）。

--*/
{
    UCHAR hidCode;   // 转换后的 HID Usage ID（键码）
    UCHAR modBits;   // 修饰键位掩码（0 表示非修饰键）

    //
    // 第一步：将 Windows 虚拟键码转换为 HID 键码和修饰位
    //
    hidCode = VirtualKeyToHidCode(KeyCode);
    modBits = GetModifierBits(KeyCode);

    if (KeyDown) {
        //
        // 按键按下处理
        //
        if (modBits != 0) {
            //
            // 修饰键按下：在 Byte 0 中设置对应的 bit（位或操作）
            // 例如：同时按住左 Ctrl 和左 Shift → Byte 0 = 0x01 | 0x02 = 0x03
            //
            ReportBuffer[0] |= modBits;
        } else {
            //
            // 普通键按下：在 Byte 2-7 中找到第一个空闲槽位（值为 0x00），写入键码
            // 如果 6 个槽位都已占用（超过 6 键同按），此次按键会被静默丢弃
            //
            for (int i = 2; i < 8; i++) {
                if (ReportBuffer[i] == 0x00) {
                    ReportBuffer[i] = hidCode;
                    break;  // 找到空槽位并填入，退出循环
                }
            }
        }
    } else {
        //
        // 按键松开处理
        //
        if (modBits != 0) {
            //
            // 修饰键松开：在 Byte 0 中清除对应的 bit（位与取反操作）
            // 例如：松开左 Ctrl → Byte 0 &= ~0x01，即清除 bit 0
            //
            ReportBuffer[0] &= ~modBits;
        } else {
            //
            // 普通键松开：在 Byte 2-7 中找到该键码，将其槽位清零
            // 清零后该槽位可以被后续的按键复用
            //
            for (int i = 2; i < 8; i++) {
                if (ReportBuffer[i] == hidCode) {
                    ReportBuffer[i] = 0x00;
                    break;  // 找到并清除，退出循环（同一键码只会出现一次）
                }
            }
        }
    }

    *ReportSize = 8;  // 标准 USB 键盘 HID 报文固定为 8 字节
}

//
// BuildMouseReport - 构建 4 字节 HID 相对鼠标报文
//
VOID BuildMouseReport(
    _In_ SHORT RelativeX,       // X 轴相对位移（超出范围会被截断到 -128~+127）
    _In_ SHORT RelativeY,       // Y 轴相对位移（超出范围会被截断到 -128~+127）
    _In_ USHORT ButtonState,    // 按键状态位掩码（bit 0=左键，bit 1=右键，bit 2=中键）
    _Out_ PUCHAR ReportBuffer,  // 指向鼠标报文缓冲区，函数完整重写前 4 字节
    _Inout_ PULONG ReportSize   // 输出参数，报文字节数（4）
)
/*++

函数说明：

    BuildMouseReport 构建符合 HID Boot Protocol Mouse Report 格式的 4 字节报文。
    与键盘报文的增量更新不同，鼠标相对移动报文每次都是完整写入（无状态累积），
    因为相对位移本身已经是增量值，不需要累积。

    注意：每次调用此函数后，调用方应立即将报文提交给 HID 子系统，
    然后再次调用时会用新的数据完整覆盖缓冲区。

--*/
{
    //
    // 截断（Clamp）X/Y 位移到有符号字节范围 [-128, +127]。
    //
    // HID 相对鼠标的 X/Y 字段为有符号 8 位整数（CHAR/INT8），
    // 若用户态传入的 SHORT（16 位有符号）超出此范围，需要截断以防止数据截断歧义。
    // 对于需要大范围快速移动的场景，应分多次发送（每次最多 ±127 像素）。
    //
    CHAR x = (RelativeX > 127) ? 127 : (RelativeX < -128) ? -128 : (CHAR)RelativeX;
    CHAR y = (RelativeY > 127) ? 127 : (RelativeY < -128) ? -128 : (CHAR)RelativeY;

    //
    // 按 HID Boot Protocol Mouse Report 格式填充报文：
    //   Byte 0：按键状态（只取低 3 位：左键 bit0、右键 bit1、中键 bit2）
    //   Byte 1：X 轴相对位移（有符号字节，正=右，负=左）
    //   Byte 2：Y 轴相对位移（有符号字节，正=下，负=上）
    //   Byte 3：滚轮位移（此处固定为 0，如需支持滚轮可扩展）
    //
    ReportBuffer[0] = (UCHAR)(ButtonState & 0x07);  // 只保留最低 3 位（按键状态）
    ReportBuffer[1] = (UCHAR)x;                     // X 位移（强转为无符号字节存储，负值会以补码形式存储）
    ReportBuffer[2] = (UCHAR)y;                     // Y 位移
    ReportBuffer[3] = 0x00;                         // 滚轮位移（暂不支持，填 0）

    *ReportSize = 4;  // 相对鼠标报文为 4 字节
}

//
// BuildAbsMouseReport - 构建 5 字节 HID 绝对鼠标报文
//
VOID BuildAbsMouseReport(
    _In_ USHORT AbsoluteX,      // X 轴绝对坐标（0=最左，65535=最右）
    _In_ USHORT AbsoluteY,      // Y 轴绝对坐标（0=最上，65535=最下）
    _In_ USHORT ButtonState,    // 按键状态位掩码（同 BuildMouseReport）
    _Out_ PUCHAR ReportBuffer,  // 指向鼠标报文缓冲区，函数完整重写前 5 字节
    _Inout_ PULONG ReportSize   // 输出参数，报文字节数（5）
)
/*++

函数说明：

    BuildAbsMouseReport 构建 HID 绝对定位鼠标报文（Absolute Mouse Report）。
    绝对坐标模式下，鼠标位置是屏幕上的固定点，而非相对偏移。

    坐标系统：
    - X 和 Y 均为 16 位无符号整数（USHORT），范围 0 ~ 65535
    - HID 子系统负责将此逻辑坐标映射到屏幕实际像素坐标
    - 例如在 1920×1080 分辨率下，X=32767 大约对应屏幕水平中点

    重要前提：
    - 本函数产生的报文格式必须与驱动的 HID 报告描述符（Report Descriptor）严格一致。
    - 报告描述符中需要声明：Usage = Absolute，Logical Min = 0，Logical Max = 65535，
      Report Size = 16，Report Count = 2（X 和 Y 各 16 位）。
    - 如果描述符与报文格式不匹配，系统会忽略报文或产生错误的坐标。

--*/
{
    //
    // 按绝对鼠标报文格式填充（5 字节）：
    //   Byte 0：按键状态（低 3 位）
    //   Byte 1：X 坐标低字节（LSB，Least Significant Byte）
    //   Byte 2：X 坐标高字节（MSB，Most Significant Byte）
    //   Byte 3：Y 坐标低字节（LSB）
    //   Byte 4：Y 坐标高字节（MSB）
    //
    // USHORT（16 位无符号整数）按小端序（Little-Endian）拆分为两个字节：
    //   低字节 = 值 & 0xFF（保留低 8 位）
    //   高字节 = (值 >> 8) & 0xFF（保留高 8 位）
    // Windows/x86 架构本身是小端序，HID 协议也采用小端序，所以此拆分是标准做法。
    //
    ReportBuffer[0] = (UCHAR)(ButtonState & 0x07);           // 按键状态（低 3 位）
    ReportBuffer[1] = (UCHAR)(AbsoluteX & 0xFF);             // X 坐标低字节
    ReportBuffer[2] = (UCHAR)((AbsoluteX >> 8) & 0xFF);      // X 坐标高字节
    ReportBuffer[3] = (UCHAR)(AbsoluteY & 0xFF);             // Y 坐标低字节
    ReportBuffer[4] = (UCHAR)((AbsoluteY >> 8) & 0xFF);      // Y 坐标高字节

    *ReportSize = 5;  // 绝对鼠标报文为 5 字节
}
