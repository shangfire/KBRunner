/*++

模块名称：

    Trace.h

模块说明：

    本文件定义了 WPP（Windows Software Trace Preprocessor，Windows 软件追踪预处理器）
    软件追踪系统所需的宏和配置。

    WPP 是 Windows 内核驱动推荐的调试日志机制，相比 DbgPrint 具有以下优势：
    - 高性能：日志数据以二进制格式写入内存环形缓冲区，对性能影响极小
    - 实时采集：可通过 TraceView、logman、WPR 等工具在运行时动态采集日志
    - 条件过滤：可按"标志位（Flag）"和"级别（Level）"精确控制哪些日志被记录
    - 无格式开销：格式字符串在离线解析阶段处理，运行时只传递原始数值

    使用方式：
      在代码中调用 TraceEvents(级别, 标志, 格式字符串, ...);
      例如：TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "设备初始化完成");

    采集工具：
      使用 TraceView（WDK 自带工具）或以下命令行采集日志：
        logman start MyTrace -p {d1a7d97c-00c2-43f4-932f-9936acbb7cc3} -o trace.etl -ets
        [运行驱动...]
        logman stop MyTrace -ets
        tracefmt trace.etl -p <PDB文件路径> -o trace.txt

运行环境：

    内核模式（Kernel Mode）

--*/

//
// WPP 控制 GUID 和追踪标志位定义
//
// WPP_CONTROL_GUIDS 宏定义了本驱动的追踪提供程序（Trace Provider），
// 每个驱动必须有全局唯一的 GUID 来标识自己的追踪会话。
//
// 追踪 GUID：d1a7d97c-00c2-43f4-932f-9936acbb7cc3
// 在采集工具中使用此 GUID 订阅本驱动的日志事件。
//
// WPP_DEFINE_BIT 定义各个追踪标志位（Flag），每个标志对应驱动中的一个子模块，
// 允许用户在采集日志时选择性地只监听某个模块的日志，减少噪音：
//   MYDRIVER_ALL_INFO - 全局通用标志，用于不属于特定模块的日志
//   TRACE_DRIVER      - Driver.c 中的驱动入口/生命周期相关日志
//   TRACE_DEVICE      - Device.c 中的设备创建/硬件初始化相关日志
//   TRACE_QUEUE       - Queue.c 中的 I/O 队列和 IOCTL 处理相关日志
//
#define WPP_CONTROL_GUIDS                                              \
    WPP_DEFINE_CONTROL_GUID(                                           \
        KMDriverTraceGuid, (d1a7d97c,00c2,43f4,932f,9936acbb7cc3), \
                                                                       \
        WPP_DEFINE_BIT(MYDRIVER_ALL_INFO)                              \
        WPP_DEFINE_BIT(TRACE_DRIVER)                                   \
        WPP_DEFINE_BIT(TRACE_DEVICE)                                   \
        WPP_DEFINE_BIT(TRACE_QUEUE)                                    \
        )

//
// 以下两个宏是 WPP 框架要求的"日志条件判断"辅助宏。
// 它们将 TraceEvents(LEVEL, FLAG, ...) 的两个控制参数（flag 和 level）
// 映射到 WPP 内部的日志器（logger）和使能检查（enabled check）。
//
// WPP_FLAG_LEVEL_LOGGER：返回与该标志关联的日志记录器句柄
// WPP_FLAG_LEVEL_ENABLED：检查该标志和级别的组合是否当前被启用，
//                         只有启用时才真正执行日志记录，确保零开销过滤
//
#define WPP_FLAG_LEVEL_LOGGER(flag, level)                             \
    WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level)                            \
    (WPP_LEVEL_ENABLED(flag) &&                                        \
     WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

//
// 以下两个宏是 WPP_LEVEL_FLAGS_* 变体，用于参数顺序为 (level, flags) 的调用场景。
// WPP 要求静态参数在动态参数之前，这两个宏确保参数顺序符合 .tpl 模板文件的预期。
//
#define WPP_LEVEL_FLAGS_LOGGER(lvl, flags) \
           WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) \
           (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

//
// WPP_RECORDER_* 宏：用于支持"inflight recorder"功能（内存环形日志缓冲区）。
// WPP 要求静态参数在动态参数之前，因此这两个宏负责将 (flags, level) 参数顺序
// 重排为 .tpl 模板文件期望的 (level, flags) 顺序，保证宏展开正确。
//
#define WPP_RECORDER_FLAGS_LEVEL_ARGS(flags, lvl)   WPP_RECORDER_LEVEL_FLAGS_ARGS(lvl, flags)
#define WPP_RECORDER_FLAGS_LEVEL_FILTER(flags, lvl) WPP_RECORDER_LEVEL_FLAGS_FILTER(lvl, flags)

//
// WPP 追踪函数声明（由 WPP 预处理器扫描此注释块并自动生成对应代码）
//
// 注意：begin_wpp config 和 end_wpp 之间的内容是特殊格式的配置指令，
// 会被 WPP 预处理器（tracewpp.exe）在编译前解析，生成 .tmh 头文件。
// 不要修改这些指令的格式，否则会导致编译失败。
//
// FUNC Trace{...}     定义了简化版追踪函数，FLAGS 固定为 MYDRIVER_ALL_INFO，
//                     适合快速添加通用日志，无需每次指定标志
// FUNC TraceEvents    定义了完整版追踪函数，需要显式指定 LEVEL 和 FLAGS，
//                     适合在具体模块中精确分类记录日志
//
// begin_wpp config
// FUNC Trace{FLAGS=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...);
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp
//
