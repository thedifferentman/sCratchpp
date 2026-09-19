# Scratch 目标 C/C++ 基础运行库

本目录提供第一批底层实现，全部作为 LLVM bitcode 链接进入项目。它不链接宿主 libc、libc++abi、Windows SDK 或操作系统服务。上层 C++ 实现来自仓库固定版本的 libc++；构建入口为 `tools/build_stdlib.py`。

## 构建和接入

默认 CMake 构建产生 `build/clang/stdlib/`，安装到 `share/scratch-llvm/stdlib/`。也可以单独执行：

```sh
python tools/build_stdlib.py --clang /path/to/clang --output-dir build/stdlib
```

构建需要 Python 3.11+、Clang 22 和同版本 `llvm-link`，全部源码已随仓库提供；不在构建时下载。C++ 用户代码使用 SDK 头文件和 Clang 的资源头文件，通过 `-nostdinc` 排除宿主头文件。VS Code 模板自动完成编译与链接，并将 libc++ 许可证加入 `.sb3`。

SDK 清单的主要字段：

| 字段 | 含义 |
| --- | --- |
| `include_dirs` | libc++ 与基础运行库的头文件目录 |
| `bitcode` / `core_bitcode` | 基础 C/C++ 运行库 `lib/scratch-stdlib.bc` |
| `llvm_major` / `target` / `scrpp_abi` | 消费预编译 scrate 包时使用的 ABI 要求 |
| `resources` / `precompiled_packages` | 兼容字段，现为空；可选库改由 scrate 管理 |

清单路径均相对 SDK，复制 SDK 后仍可构建。Console、Events、PTE 是普通 scrate 包，不再随 SDK 自动链接；项目通过 `[dependencies]` 获取这些包。更新旧 SDK 时会清理过去生成的三个库头文件目录、资源目录和合并 bitcode。修改基础 C/C++ 运行库后重建 SDK；修改本地包后重新运行项目构建以更新锁文件。

第一批采用 libc++ 22.1.8、Itanium C++ ABI、LP64、小端、64 位指针。SDK 禁用异常、RTTI、线程、wide characters 和文件系统，提供固定 C locale 的窄字符流与数值转换。ABI 2 使用 `-mlong-double-64`，不能与旧 ABI 1 的预编译 bitcode 混用；源码包会重新编译。详见 [iostream](../../docs/IOSTREAM.md)。

## 内存管理

`src/runtime.cpp` 在虚拟字节内存中预留一个静态、`max_align_t` 对齐的堆。`SCRATCH_HEAP_BYTES` 默认为 16384，必须至少 128 且为 `alignof(max_align_t)` 的倍数。此容量包含分配块元数据；它与其他全局对象、栈共同占用编译器的总内存容量，不能把总内存容量全部设置为堆大小。

- `malloc/free` 使用首次适配，分割空闲块并合并物理相邻的空闲块。
- `realloc` 优先缩小或合并后继空闲块，否则分配、复制再释放；失败保留原对象。
- `calloc` 检查乘法溢出并清零。
- `aligned_alloc` 要求二的幂次对齐且大小为对齐值的倍数；`posix_memalign` 另要求对齐至少为指针大小。
- 零大小 `malloc` 按一字节请求处理；`realloc(p, 0)` 释放并返回空指针。
- 分配失败返回空指针和 `ENOMEM`；无效 `aligned_alloc` 参数设置 `EINVAL`。`posix_memalign` 保持原 `errno`，失败不改输出指针。
- 无效释放和重复释放会触发 trap；这些操作在 C/C++ 中本来属于未定义行为。

分配、释放与合并的最坏开销与现有块数成正比。没有并发、压缩移动、操作系统申请内存或文件系统依赖。

`src/operators.cpp` 提供 C++ 标量/数组、带大小、对齐及 `nothrow` 的分配释放接口，以及 `new_handler`。有处理器时分配失败会调用处理器并重试；无处理器时普通 `new` 终止，`nothrow new` 返回空指针。

分配释放定义可被用户强符号替换。与 libc++ 的无异常模式一样，**替换普通 `new` 时必须同时替换相应 `nothrow new`**。默认 `nothrow` 实现检测不匹配替换并终止，以免从错误的堆分配、再交给用户释放函数。数组路径也检查它所依赖的标量分配器。

## 基础函数与生命周期

实现字节复制/移动/填充/比较、常用窄字符字符串处理、基础整数绝对值/商余数、二分查找与不分配内存的堆排序。`strcoll/strxfrm` 目前仅采用经典字节序比较；没有可变 locale。

单执行上下文的 `errno`、断言、`abort`、`terminate`、纯虚函数失败均有实现。终止式失败使用 LLVM trap；没有异常展开，断言目前不输出动态诊断文本。

全局初始化沿用 LLVM `global_ctors`，局部静态初始化提供 Itanium ABI 的 `__cxa_guard_*`，递归初始化触发 trap。析构登记提供 `__cxa_atexit/__cxa_finalize/atexit`；默认最多 128 项未清理登记，可通过 `SCRATCH_ATEXIT_CAPACITY` 修改。顺序为后登记先调用；回调中新增登记和重入清理不会重复执行同一条目。

- `main` 正常返回：编译器执行 `global_dtors`，本库的 finalizer 清理所有登记析构。
- `exit`：清理登记析构并刷新标准输出后报告退出码、停止 Scratch 项目。
- `_Exit`：直接报告退出码并停止，不执行析构。
- `quick_exit`：仅执行独立的 `at_quick_exit` 登记，再停止。

显式 `exit` 暂不调用与登记表无关的 GNU `__attribute__((destructor))` 函数；这种扩展仍会在 `main` 正常返回时由编译器运行。没有异常栈展开、线程退出析构、动态库卸载和 RTTI。

## 范围与编译约束

`scratch_platform.hpp` 提供 `bool scratch::is_turbowarp()`（模板的 `scratch.hpp` 同样声明此函数）。它调用兼容的布尔参数报告器 `is turbowarp?`，不加载 TW 扩展；原版返回假，TW 编译和解释模式均返回真。库通过这个接口选择控制台的键盘编辑行为。

这不是完整 libc。头文件中为 libc++ 解析而提供的转换、格式化、环境等声明，**不代表已有实现**；若用户实际调用未提供的符号，最终链接/编译应报告未解析符号。标准输入输出和经典 locale 已实现首版；完整数学库、命名地区 locale 和文件系统仍未实现。

运行库必须用 `-ffreestanding -fno-builtin` 构建，防止内存函数循环被优化为调用自身。SDK 的所有模块使用相同的 x86_64 LP64、小端、64 位指针布局和 libc++ 配置，禁用异常、RTTI 和线程；不能混入宿主目标的二进制库。

普通用户代码不加 `-ffreestanding`，可直接写 `int main()`；这不表示目标拥有完整 hosted 标准库。生成的指令仍须在编译器支持范围内。Clang 为对齐分配产生的 `llvm.assume` 的 `align` 附加信息，以及别名分析提示 `llvm.experimental.noalias.scope.decl` 已接受，不生成运行操作；其他带状态的 call operand bundle 继续拒绝。

集成测试见 [tests/stdlib](../../tests/stdlib/README.md)。目前复杂 STL 模板可能生成很大的积木图：组合 vector/deque/list/string 的测试曾产生约 90 MB 项目并超过原版 VM 的 60 秒测试限时。独立功能通过不代表大型组合程序已经达到实用性能；测试保留了该压力用例。
