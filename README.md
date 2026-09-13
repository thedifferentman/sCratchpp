# s·C·ratch++

将 LLVM IR 编译成原版 Scratch 3 项目的 C++ 编译器。输出为 `.sb3`，使用官方积木；主要面向 TurboWarp，同时通过原版 Scratch VM 验证。

项目采用真实 LLVM 解析、验证和链接，字节列表内存、精确逐字节整数运算、函数指针二分分派和 warp 自定义积木。它生成实际运算积木，不在项目中逐条解释 LLVM 指令。

后端的基本操作已采用 [共享槽位运行时](docs/SLOT_RUNTIME.md)：指令调用传递 SSA 槽位地址，复用数值和访存过程；常量、槽位视图及 phi 复制计划由编译器处理。原有精确类型、字节内存和 SoftFloat 语义保持不变。

共享槽位改造的对照测量中，模板在相同 O2 与裁剪设置下由 131,100 块降至 20,521 块；验证与性能测量见 [槽位运行时验证记录](docs/SLOT_RUNTIME_RESULTS.md)。

完整设计见 [DESIGN.md](DESIGN.md)。实际测试记录见 [docs/TEST_RESULTS.md](docs/TEST_RESULTS.md)。设计路线中的功能不自动等于当前支持能力，以下以实现为准。

最新宿主验证见 [跨平台验证记录](docs/CROSS_PLATFORM.md)：Linux 和独立 Windows Clang 构建均通过 50 组完整回归。

## 构建

宿主构建使用 **Clang＋libc++＋LLD＋Ninja**，不依赖 Visual Studio。已经实际验证 Linux x86_64 与独立 Windows Clang SDK；macOS 提供平台代码和配置路径，尚未实机验证。完整准备、安装与配置说明见 [跨平台构建](docs/BUILDING.md)。

需要：

- Clang GNU 风格驱动、C++17 标准库与 ABI 库、LLD、Ninja、CMake 3.24 或更新版本。
- LLVM 22.1 系列的 C API 共享库及链接库，通过 `LLVM_ROOT` 或显式路径提供。
- Python 与配套 Clang，用于从随附的纯 C 源码构建精确浮点运行时。
- 若运行真实 VM 测试，还需 Node.js 20 或更新版本及测试依赖。

```sh
cmake --preset clang
cmake --build --preset clang --parallel 6
```

编译器位于 `build/clang/scratch-llvm`，Windows 加 `.exe` 后缀。CMake 会构建并复制 `scratch-float.bc` 和许可；Windows 自动复制实际依赖的 SDK DLL，Linux/macOS 使用配置好的共享库路径。浮点运行时不包含宿主浮点指令或机器汇编；它也通过同一字节后端编译。

可独立准备完整工具链，不修改系统软件：

```powershell
# Windows x64：Clang、libc++、LLD、MinGW CRT、LLVM C API、CMake、Ninja
python tools/bootstrap_windows.py
. ./build/toolchains/windows/activate.ps1
```

```sh
# Ubuntu 24.04 x86_64：校验官方包并解包到用户缓存，无需 sudo
python3 tools/bootstrap_linux.py --with-node
. "$HOME/.cache/scratch-llvm/llvm-22.1.8-noble/activate.sh"
```

仅构建整数后端时可传 `-DSCRATCH_BUILD_FLOAT_RUNTIME=OFF`。此时涉及需要运行时的浮点操作会要求提供 `--float-runtime`，不会退化为不精确计算。

切换宿主或工具链时使用新构建目录。旧 `build/native` 为历史 MSVC 产物，不再作为默认构建或测试入口。

## 编译和运行

```sh
./build/clang/scratch-llvm tests/fixtures/integer.ll -o build/integer.sb3
./build/clang/scratch-llvm first.ll second.bc -o build/linked.sb3
```

本文通用命令在 Windows 上给可执行文件加 `.exe` 后缀即可。

在 Scratch 或 TurboWarp 中上传生成的 `.sb3`，点击绿旗。程序完成后，执行角色中可检查：

- `__scl_status`：`done` 或具体错误状态。
- `return_bytes`：`main` 返回值的低字节优先位模式。
- `exit_code`：不超过四字节的返回值按无符号数汇总；更宽的返回值以 `return_bytes` 为准。

每次绿旗重新初始化内存和执行状态。内部列表和变量默认没有监视器，以免监视器更新干扰运行。

支持 `main()` 和 `main(i32, ptr)` 入口。后者的 `argv[0]` 为 `program`，字符串按 UTF-8 字节写入内存，指针数组以空指针结尾：

```sh
./build/clang/scratch-llvm program.ll -o build/program.sb3 --arg "你好" --arg 123
./build/clang/scratch-llvm program.ll -o build/program.sb3 -- "你好" 123
```

其他选项：

```text
--dump-ir path.json    导出规范化、完成必要降级的后端输入
--debug-map path.json  导出源码、IR、槽位和积木位置映射，不插入调试积木
--passes pipeline     指定 LLVM 优化流水线；默认不额外执行用户优化
--whole-program       最终程序内部化和全局死代码裁剪；自动保留入口及所需浮点辅助函数
--data-layout layout  为缺失布局的模块显式提供布局
--memory bytes        程序内存容量，1024～200000，默认65536
--float-runtime path  指定兼容的浮点运行时bitcode
--runtime-dir path    指定同时包含bitcode和许可证的目录
--version             查看编译器版本
--help                查看帮助
```

输入缺少 DataLayout 时会明确报错。普通输入模块需具有匹配的布局与目标配置；编译器自己的浮点运行时在逐类型验证兼容后才归一布局并参与 LLVM 链接。

VS Code 模板默认使用 `--passes 'default<O2>' --whole-program`。该模式在每次 LLVM 链接与优化后，从实际入口集合生成内部化保留名单，再执行 `globaldce`；包括浮点降级前暂时没有调用者的辅助入口。它面向完整程序，不能用于需要继续向外导出任意函数的库构建。未启用时保持原有保守裁剪行为。

支持 `cmake --install build/clang --prefix ...`。程序从实际可执行位置或安装数据目录查找运行时，不再依赖当前工作目录；也可用 `SCRATCH_RUNTIME_DIR` 指定。显式错误路径会报错，不能静默加载另一份运行时。

## 从 C 生成输入

VS Code 用户可复制 [template/](template/README.md) 作为 C++ 工程，配置工具路径后按 `Ctrl+Shift+B` 生成 `.sb3`；模板包含画笔示例、多源文件构建及代码补全配置。

模板同时提供 `Scratch: Debug` 任务和 F5 调试配置，以及 `Scratch: Run in TurboWarp` 直接运行配置。安装随附的 VSIX 后，真实 LLDB 通过 IR 远程目标控制 TW 解释器，支持动态源码断点、暂停/继续、源码单步、调用栈及基本变量。调试自动关闭 TW 编译，不依赖 Debugger 插件，不固定 TW 版本。安装及首版限制见 [调试器说明](debugger/README.md)。

C 示例无需系统头文件或标准库：

```sh
cmake -E make_directory build/examples
clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O1 examples/fibonacci.c -o build/examples/fibonacci.ll
./build/clang/scratch-llvm build/examples/fibonacci.ll -o build/examples/fibonacci.sb3
```

`fibonacci.sb3` 的结果为 144。画笔示例使用同样流程：

```sh
clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O1 examples/pen_spiral.c -o build/examples/pen_spiral.ll
./build/clang/scratch-llvm build/examples/pen_spiral.ll -o build/examples/pen_spiral.sb3
```

Scratch 汇编使用官方 opcode，不加额外前缀；可使用命名输入、嵌套 reporter、条件与循环子脚本。详见 [汇编接口](docs/ASSEMBLY.md)。机器汇编即使位于不可达函数中，也会在优化前被拒绝。

## 当前支持范围

默认构建同时提供 `build/clang/stdlib/`：固定 libc++ 22.1.8 的目标头文件、基础 C/C++ 运行库与 bitcode。它在 LLVM 层链接，不使用宿主标准库二进制。VS Code 模板会自动发现该 SDK，现在可以直接包含 `<vector>`、`<string>`、`<map>`、`<algorithm>` 等常用头文件。

这批实现包括可回收的 16 KiB 堆、对齐分配、`new/delete`、基础内存和窄字符串函数、全局/局部静态对象生命周期、`atexit/exit` 与终止式错误处理。配置禁用异常、RTTI 和线程；输入询问、Console 列表输出、数学库及虚拟文件系统留到后续批次。具体接口、容量和限制见 [目标基础运行库](runtime/stdlib/README.md)。

可单独执行 `python tools/build_stdlib.py --clang /path/to/clang --output-dir build/stdlib`。CMake 可用 `-DSCRATCH_BUILD_STDLIB=OFF` 关闭 SDK 构建；`SCRATCH_HEAP_BYTES` 控制目标堆容量。标准库测试为 `ctest --test-dir build/clang -R '^stdlib$' --output-on-failure`。

| 范围 | 实现情况 |
| --- | --- |
| 输入 | `.ll/.bc`、LLVM Verifier、多模块链接、可达性裁剪、别名与弱外部引用 |
| 布局 | 小端、字节内存、结构体/packed结构体/数组、未对齐访问、GEP索引宽度 |
| 整数 | 任意固定 iN 的加减乘除余、比较、转换、位运算、移位；已测至257位 |
| 浮点 | float/double及固定向量的基础运算、16种比较、FMA/sqrt/min/max等；整数转换目前至64位 |
| 控制流 | 分支、switch、循环、phi并行复制、基本块状态分派、blockaddress/indirectbr |
| 调用 | 普通调用、递归、函数指针、聚合返回、sret/byval、有限栈空间的musttail转移 |
| 栈 | 动态alloca、stacksave/stackrestore，与SSA存储分开回收 |
| 变参 | SysV x86_64标量ABI，va_start/va_arg/va_copy/va_end，支持寄存器区域及溢出参数区域 |
| 内存与全局 | load/store、memcpy/memmove/memset、全局初始化、构造/析构入口、TLS |
| 原子 | 单执行上下文的整数atomicrmw/cmpxchg/load/store/fence；保留volatile访问 |
| 向量 | 固定整数向量、通道操作、shuffle、整数intrinsic、整数reduce；字节对齐通道的masked访存 |
| intrinsic | 位计数、字节/位反转、整数min/max、溢出/饱和算术、funnel shift等 |
| 汇编与项目 | 官方积木方言、空汇编屏障、画笔扩展、单执行角色、warp、确定性SB3与许可附件 |

当前边界包括：大端和非普通地址空间、函数prefix/prologue、地址零有效配置、异常/funclet/SJLJ、preallocated/inalloca、callbr、部分operand bundle、变参聚合分类和变参musttail、特殊浮点格式和constrained FP、GC及其他延后机制。它们需要独立实现与验收；当前遇到会给出诊断。

未提供的普通函数定义不会被按名字伪装成运行时实现。标准库、文件系统、线程、网络等接口需要另外链接或适配，不能因为 IR 能解析就宣称对应库可运行。完整 Clang 自举尚未验证。

## 测试

```sh
npm --prefix tests ci --ignore-scripts
cmake --preset clang
cmake --build --preset clang --parallel 6
ctest --preset clang
```

也可以独立运行：

```sh
npm --prefix tests test
node tests/e2e.cjs --compiler build/clang/scratch-llvm --output-dir build/clang/test-output/e2e --vm both --timeout 60000
node tests/vm_runner.cjs build/integer.sb3 --vm scratch --timeout 30000
node tests/vm_runner.cjs build/integer.sb3 --vm turbowarp --timeout 30000
python tools/build_float_runtime.py --host-test
```

测试区分精确算术参考、AST执行、真实原版VM、TurboWarp实际编译执行和诊断负例。VM测试不依赖修改后的opcode，也不通过提升默认Node堆上限掩盖资源问题。无renderer模式验证计算、资源加载和画笔操作执行，不验证绘图像素。

含SoftFloat的项目仍可能具有较大的积木规模与TurboWarp首次编译成本。测试报告分别记录首次线程编译、执行阶段、输出规模与峰值内存；不能将微基准比例当作所有程序的性能倍率。

## 依赖和许可

- LLVM C API头：22.1.8，许可见 `third_party/llvm/LICENSE.txt`；使用系统或独立前缀内的匹配LLVM共享库。
- nlohmann/json：3.12.0，许可见 `third_party/nlohmann/LICENSE.txt`。
- Berkeley SoftFloat：Release 3e，固定源码和SHA-256清单见 `runtime/softfloat/`。
- 真实VM测试依赖固定在 `tests/package-lock.json`。

生成的含SoftFloat运行时的SB3自动包含 `licenses/SoftFloat.txt`。`tools/fetch_dependencies.py` 仅用于维护时从官方来源恢复固定版本头文件，正常构建无需运行。
