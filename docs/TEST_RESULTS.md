# 实现验证记录：2026-09-13

本记录对应一次已经完成的固定构建回归，不代表任意 LLVM IR 或任意现有库均已兼容。

这是首版 MSVC 构建的历史记录。当前 Clang 跨平台构建与最新验证见 [CROSS_PLATFORM.md](CROSS_PLATFORM.md)；保留本记录用于追溯，不将旧二进制视为新构建的测试对象。

## 1. 本次结论

- **50/50 个端到端 fixture 通过。** 其中 40 个正例分别在原版 Scratch VM 与 TurboWarp 编译模式运行，共 **80 次真实 VM 执行**；10 个负例均产生预期诊断且没有留下 SB3。此前 42 项和 46 项回归的原始报告仍保留为历史记录。
- 所有正例均检查 `__scl_status=done`、`exit_code` 和完整 `return_bytes`；所有 TurboWarp 正例均观察到实际编译的线程。
- 本次没有非预期的编译失败、结果不符、运行时错误、超时或 JavaScript 堆耗尽。
- 3 个 `musttail` 正例在 **4096 字节程序内存**下完成 2500 次以上的自尾调用、相互尾调用或 `byval` 转移。
- 新增 `argc/argv`、UTF-8 参数字节、masked load/store/gather/scatter 的关闭通道和重叠写入检查，两个 VM 全部通过。
- 独立 CTest **7/7 通过**，包括此前失败的 `frontend-reachability`；完整清理重建后的复验已确认通过，见第 6 节。
- 独立审查新增的向量 GEP、构造器别名、Clang 自动 clobber 和 4 GiB 布局容量检查均通过。
- 共享数值 helper 与向量按字节打包优化后，先前发生 TurboWarp 堆耗尽的浮点正例均在默认堆上限下通过。浮点项目的生成体积和 TurboWarp 编译内存仍然显著，见第 5 节。

## 2. 构建、时间与环境

| 项目 | 本次记录 |
| --- | --- |
| 测试开始 | 2026-09-13 02:11:17.453，Asia/Shanghai（UTC+08:00） |
| 测试总耗时 | 167.695 秒，包括各次编译、进程启动、加载和执行 |
| 编译器 | `scratch-llvm 0.1.0`，`build/native/Release/scratch-llvm.exe` |
| 编译器文件时间 | 2026-09-13 02:08:21.477，UTC+08:00；50 项记录均使用此文件时间 |
| 主机 | Windows x64 |
| LLVM / Clang 开发环境 | 22.1.8；Clang commit `ca7933e47d3a3451d81e72ac174dcb5aa28b59d1` |
| Node.js | v24.14.0 |
| 原版 Scratch VM | npm `scratch-vm@5.0.300`，解释执行 |
| TurboWarp VM | 官方 `TurboWarp/scratch-vm` commit `c4823421cb7c17d8d8a89878851ce1668c26a21f`，启用编译器 |
| 资源加载 | npm `scratch-storage@6.2.1` |
| VM 配置 | compatibility mode 开启、turbo mode 开启；TurboWarp `warpTimer:false` |
| 每个正例的执行期限 | 60000 毫秒；控制进程能终止不让出执行的 warp 子进程 |
| Node 堆上限 | 报告值 4288 MiB；没有添加提高堆上限的选项 |

编译器 SHA-256：

```text
b3d04c661ea3d491e1d41cd39f04bfd76a0578185b2e688f6ea9ab2ad4d151ef
```

本次 fixture 清单 SHA-256：

```text
0de93d99e75e4c2a239fc9ca63fb3f1433121a2e78b444a17f133b5a973c4f26
```

本次使用的 `scratch-float.bc` SHA-256：

```text
a5de6984f400f4cd795b7b29a4232cc61a591cb018e18fa6600ea494b0db1c7a
```

## 3. 执行方式与判定

入口是 [tests/e2e.cjs](../tests/e2e.cjs)，用例及预期值来自 [manifest.json](../tests/fixtures/manifest.json)。每个正例经历：

```text
文本 LLVM IR → 本次 scratch-llvm → SB3
    → 全新原版 VM 进程加载、点击绿旗、运行至无活动线程、检查结果
    → 全新 TurboWarp VM 进程加载、编译并执行、检查结果
```

[vm_runner.cjs](../tests/vm_runner.cjs) 使用真实 VM 的执行器和真实 storage，没有用 JavaScript 重写或模拟待测 Scratch 算术、调用、列表与控制流指令。TurboWarp 的 `COMPILE_ERROR` 导致失败，不能退回解释器后仍报告成功。

SVG 资源通过真实 storage 加载；没有连接 renderer 或 audio engine。本次验证的是项目加载和计算语义，**没有验证画笔像素、声音、浏览器编辑器交互或浏览器部署**。

报告保留 Scratch 与 TurboWarp 原始的数字/数字字符串差别，比较数值结果时进行规范化。`--list-limit 256` 只限制输出报告的列表预览；完整列表长度仍记录，四字节 `main` 返回值完整检查，执行中的内存没有被截断。

负例要求编译器非零退出、诊断匹配且没有输出产物。文件读取失败不能冒充预期语义诊断。报告分别保留编译失败、VM 错误、结果不符和负例诊断错误；JavaScript 堆耗尽额外标记为资源失败。

## 4. 本次覆盖

### 4.1 正例

下面每一项都已在两个 VM 通过；表中为完整 `main` 的 i32 结果。

| 分组 | Fixture | 预期结果 |
| --- | --- | ---: |
| 标量整数、位运算 | `integer.ll` | 207 |
| 递归、跨调用 SSA 值 | `recursion.ll` | 775 |
| 字节别名、GEP、phi 交换 | `memory_phi.ll` | 61 |
| 函数指针表、间接调用 | `function_pointer.ll` | 47 |
| i64 乘除、奇数位宽、符号扩展 | `integer_wide.ll` | 127 |
| packed 聚合、未对齐访问、全局别名 | `global_aggregate.ll` | 300 |
| 聚合返回、sret、byval 副本隔离 | `call_aggregate.ll` | 31 |
| atomic、cmpxchg、TLS、volatile | `atomic_tls.ll` | 41 |
| memcpy、重叠 memmove、memset | `memory_intrinsics.ll` | 44 |
| 动态 alloca、stackrestore、活跃 SSA 值 | `stack_restore.ll` | 37 |
| i3 向量、shuffle、选择、位打包 | `vector_bits.ll` | 35 |
| 构造器优先级、析构路径 | `global_ctors.ll` | 50 |
| 溢出 intrinsic、位计数、饱和运算 | `intrinsic_overflow.ll` | 63 |
| i1/i9 读取时的无效高位 | `narrow_load.ll` | 3 |
| 64 位指针、32 位 GEP 索引 | `numeric_gep_index.ll` | 0 |
| 零长度访问与不解引用的大指针 | `numeric_zero_length.ll` | 0 |
| i5 向量、嵌套聚合布局 | `numeric_vector_odd.ll` | 0 |
| i64 原子 min/max/NAND | `numeric_atomic.ll` | 0 |
| 不被选中的向量 poison | `numeric_vector_poison.ll` | 0 |
| 向量位计数、abs、溢出 intrinsic | `numeric_vector_intrinsic.ll` | 0 |
| 2500 次自尾调用，4 KiB 内存 | `musttail_self.ll` | 2500 |
| 2501 次相互尾调用，4 KiB 内存 | `musttail_mutual.ll` | 6252 |
| 2500 次 byval 尾转移，4 KiB 内存 | `musttail_byval.ll` | 2507 |
| float/double 算术与转换 | `float_arithmetic.ll` | 36 |
| FMA、浮点 min/max、NaN、正负零 | `float_intrinsics.ll` | 37 |
| 固定浮点向量 | `float_vector.ll` | 38 |
| 16 种 fcmp 谓词 | `float_compare.ll` | 39 |
| fneg/fabs/copysign 与 NaN payload | `float_signbits.ll` | 40 |
| 命名汇编、输入桥接、嵌套 reporter、控制模板 | `assembly_named.ll` | 42 |
| 原始 va_arg、递归、va_copy、混合标量 | `varargs_ir.ll` | 58 |
| Clang SysV va_list、间接调用、GP/FP 溢出区域 | `varargs_sysv.ll` | 57 |
| 全局 blockaddress 重定位 | `blockaddress_global.ll` | 42 |
| indirectbr 前驱边及循环回边上的 phi | `indirectbr_phi.ll` | 36 |
| argc/argv、显式参数、末尾空指针 | `main_args.ll` | 68 |
| `-- é`、UTF-8 C3 A9 00、argc、argv[0] | `main_args_utf8.ll` | 197 |
| masked load/store/gather、关闭无效指针通道 | `masked_memory.ll` | 367 |
| 重叠 scatter 顺序、关闭无效指针通道 | `masked_scatter.ll` | 606 |
| 向量 GEP 的 base/index 各种组合 | `review_vector_gep.ll` | 20 |
| 通过函数别名调用构造器 | `review_ctor_alias.ll` | 7 |
| 真实 Clang 生成的 dirflag/fpsr/flags clobber | `clang_asm_clobbers.ll` | 42 |

Masked fixture 的声明已由本机 Clang 22.1.8 解析确认，使用不带旧 `i32 alignment` 参数的签名。测试关闭的通道含 `-1` 和 `2^32` 编码指针，且检查 passthrough 值。Scatter 的两个启用通道写向同一地址，预期后面的通道覆盖前面的通道，符合 [LLVM 对重叠 scatter 的顺序规定](https://llvm.org/docs/LangRef.html#llvm-masked-scatter-intrinsics)。

### 4.2 负例

| Fixture | 实际验证的拒绝原因 |
| --- | --- |
| `negative/no_datalayout.ll` | 缺少 DataLayout，且没有显式提供后备配置 |
| `negative/big_endian.ll` | 当前目标不支持大端布局 |
| `negative/machine_asm.ll` | x86 机器汇编不是支持的 Scratch 汇编 |
| `negative/invalid_ssa.ll` | LLVM Verifier 检出定义不支配使用 |
| `negative/unused_asm_suffix.ll` | `globaldce` 之前仍须检查未使用函数中的完整汇编模板，不能隐藏第二条非法机器指令 |
| `negative/float_half.ll` | 当前浮点降级范围为 IEEE float/double |
| `negative/float_wide_integer.ll` | 整数到浮点转换超出当前 1…64 位输入支持范围 |
| `varargs_negative_windows.ll` | 当前变参 ABI 只支持规定的 x86-64 System V 配置 |
| `varargs_negative_aggregate.ll` | 当前变参配置拒绝聚合、向量及未支持的宽参数 |
| `negative/oversized_alloca.ll` | 4 GiB 分配类型的大小在窄化前触发容量诊断，不能截断为零 |

## 5. 最终构建的资源与时间

以下均来自第 2 节同一构建的本次完整回归。MB 按 1000000 字节计，MiB 按 1048576 字节计。每个用例每个 VM 只运行一次，没有统计多轮中位数。

`executionMs` 从绿旗开始，包含 TurboWarp 线程编译和调度等待，不包括项目加载。`threadCompilationMs` 测量真实 VM 创建已编译线程的时间；相减后的数值仍包含调度与 JavaScript 引擎执行/JIT，不能当作纯算法 CPU 时间。

| 用例 | SB3 / MB | 原版执行 / ms | TW 执行合计 / ms | 其中 TW 线程编译 / ms | 相减后 / ms | TW 峰值 RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| float_arithmetic | 28.97 | 17484.3 | 9662.3 | 9550.7 | 111.6 | 1876 |
| float_intrinsics | 21.25 | 12842.8 | 13210.8 | 13122.7 | 88.2 | 2333 |
| float_vector | 14.55 | 5844.8 | 5137.6 | 5096.0 | 41.6 | 950 |
| float_compare | 2.81 | 373.7 | 282.3 | 264.9 | 17.4 | 209 |
| float_signbits | 1.04 | 80.6 | 43.7 | 28.9 | 14.8 | 96 |
| varargs_ir | 1.60 | 155.7 | 101.7 | 88.8 | 12.9 | 136 |
| varargs_sysv | 4.71 | 506.2 | 1102.7 | 1083.1 | 19.7 | 418 |
| musttail_self | 0.19 | 210.0 | 36.4 | 24.7 | 11.7 | 85 |
| musttail_mutual | 0.28 | 229.0 | 44.3 | 29.1 | 15.2 | 93 |
| musttail_byval | 0.34 | 339.5 | 47.5 | 29.6 | 17.9 | 99 |
| masked_memory | 1.81 | 146.4 | 415.9 | 405.8 | 10.1 | 255 |
| masked_scatter | 1.08 | 89.8 | 140.3 | 132.7 | 7.6 | 162 |

浮点 arithmetic/intrinsics 的 TurboWarp 成本主要发生在线程编译阶段，峰值进程内存仍达到约 1.83/2.28 GiB。这个结果证明本次样例可在默认 Node 堆配置内完成，不证明更大型库或完整 Clang 能在相同预算内运行。

历史回归中，未共享数值 helper 的 `float_arithmetic` 为 48.10 MB，`float_intrinsics` 为 32.62 MB，二者在默认 Node 堆下发生 OOM。对应旧报告原样保留。随后共享 helper 消除了这些样例的 OOM。向量字节快路径曾将 `float_vector` 从 27.82 MB 降至 9.02 MB；本次后续语义修复后的实际值为 14.55 MB，不能沿用上一轮较小的产物或时间数字。不同轮次的单次时间仅作定位参考，不作为严格性能倍率结论。

## 6. 其他已执行检查与统计边界

- 当前保留的 `build/native/Testing/Temporary/LastTest.log` 记录了 2026-09-13 02:08 的七项 CTest，**7/7 通过**：`blocks`、`prune`、`assembly`、`numeric`、`shared-numeric`、`frontend-reachability`、`assembly-vm`。此前 `frontend-reachability` 曾报告 `JSON pruning discarded a conservative LLVM-level address candidate`；完整清理重建后的该项检查已经实际通过，不再列为待解决失败。
- `numeric` 报告 34736 个整数边界/随机 **AST 级参考检查**；这不是 34736 次真实 Scratch VM 执行。`shared-numeric` 检查共享 helper 复用与结果隔离并生成 135 个 helper、684 字节检查的 VM 项目。`assembly-vm` 分别在两个 VM 验证 16 项数值/控制桥接与画笔项目加载执行，没有验证像素。
- [vm_runner.test.cjs](../tests/vm_runner.test.cjs) 的 5 项真实 VM 工具自测通过：两个 VM 的 warp 自定义积木和列表 reporter 布尔连接、两个 VM 的无限 warp 强制超时、无效 SB3 错误处理。这 5 项不计入上述 50 个编译器 fixture。
- Windows 含 `s·C·ratch++` 的绝对输入与绝对输出路径已单独成功编译并确认输出存在；完整回归自身使用相对输入/输出参数。
- 本次 50 项使用显式 Node 端到端命令运行，其他 CTest 结果来自实际保留日志，分别统计。

这些检查覆盖选定的定义良好输入与明确拒绝的输入。没有进行整个 LLVM IR 语义空间的穷举，也没有验证完整高级语言标准库、操作系统接口、异常运行时、并发、GC、可伸缩向量、Clang 自举或后端自举。本次也没有进行浏览器内存与桌面交互体验基准。

## 7. 复现与原始报告

从仓库根目录执行：

```text
npm --prefix tests ci --ignore-scripts
npm --prefix tests test
node tests/e2e.cjs --compiler build/native/Release/scratch-llvm.exe --vm both --timeout 60000 --report tests/.tmp/e2e/review-full-report.json
```

单独回归示例：

```text
node tests/e2e.cjs --compiler build/native/Release/scratch-llvm.exe --vm both --case "musttail_*" --timeout 60000
node tests/e2e.cjs --compiler build/native/Release/scratch-llvm.exe --vm both --exclude "*float*" --timeout 60000
```

原始输出位于本地忽略目录 `tests/.tmp/e2e/`，包括每次 VM 的变量、返回列表、内存预览、编译模式、资源指标、错误日志及每个 fixture 的输入哈希。这些运行产物不随源码自动分发；本文件保存了本次摘要，重新运行命令可生成新的完整报告。

| 报告 | 用途 |
| --- | --- |
| `review-full-report.json` | 本文的 50 项独立审查后完整回归，首要数据来源 |
| `review-targeted-report.json` | 本次全量前向量 GEP、ctor alias、Clang clobber、容量诊断的 4 项补测 |
| `freeze-full-report.json` | 前一构建的 46 项冻结功能回归，保留为历史记录 |
| `freeze-targeted-report.json` | 本次全量前新增 argc/argv 和 masked 内存的 4 项补测 |
| `final-full-report.json` | 前一构建的 42 项完整回归，保留为历史记录 |
| `final-targeted-report.json` | 最终完整回归前，7 项变参、间接跳转及完整汇编检查 |
| `shared-nonfloat-report.json` | 共享 helper 后的早期非浮点回归 |
| `shared-float-report.json` | 共享 helper 后、向量字节快路径前的浮点资源回归 |
| `float-report.json` | 共享 helper 前的浮点 OOM 原始记录 |
| `narrow-load-report.json` | 窄整数读取高位缺陷的初始失败记录 |
| `assembly-report.json` | 完整预优化汇编检查接入前的初始失败记录 |

旧失败报告用于保留问题证据；是否修复以本次完整回归中相应的明确通过结果为准。
