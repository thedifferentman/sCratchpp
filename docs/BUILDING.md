# Clang 跨平台构建

默认构建另会生成面向 Scratch 的标准库 SDK：`build/clang/stdlib/`。它与下文用于构建编译器本身的宿主 libc++ 分开。需要同版本 Clang 22 和 `llvm-link`，所提供的独立工具链均包含它们。安装时 SDK 位于 `share/scratch-llvm/stdlib/`，包含目标头文件、bitcode、清单和许可证。参见 [基础运行库](../runtime/stdlib/README.md)。可用 `-DSCRATCH_BUILD_STDLIB=OFF` 关闭，或用 `-DSCRATCH_HEAP_BYTES=32768` 调整目标堆；总虚拟内存仍需给全局数据和调用栈留出空间。

编译器宿主构建统一使用 **Clang GNU 风格驱动＋C++17 标准库＋独立链接器＋Ninja**。默认标准库是 libc++，默认链接器是 LLD。主业务逻辑不再包含 Windows API；平台差异隔离在 `src/platform.cpp`。

本次实际验证 Linux x86_64（Ubuntu 24.04 / WSL2）和 Windows x86_64 独立 Clang 工具链。macOS 有对应平台代码与构建路径，但本次没有 macOS 主机，不能将其标为已实机验证。

## 1. 分清两套目标

| 部分 | 作用 | 平台约束 |
| --- | --- | --- |
| 宿主 Clang/clang++ | 把本项目 C++ 源码编译成可执行文件 | 使用当前宿主或显式交叉编译目标的工具链 |
| libc++/libc++abi、展开库及编译器运行库 | 宿主 C++17、异常和基础运行支持 | 与宿主 ABI 匹配，独立准备 |
| LLD、llvm-ar、Ninja、CMake | 宿主链接、归档和构建 | 使用相应宿主可运行的版本 |
| LLVM 22 C API 共享库/导入库 | 在宿主内解析、链接和处理输入 IR | 必须与宿主架构匹配，不能用另一系统的 DLL/.so |
| `scratch-float.bc` | 目标程序的纯整数软件浮点实现 | 当前 guest ABI 为小端 64 位；不是宿主动态库 |
| Node.js 20+ | CLI 运行/调试及真实 VM 测试 | VS Code 内嵌播放器默认使用 VS Code 自带 Node，无需另外安装 |

Windows 宿主可以处理 `x86_64-unknown-linux-gnu` 的 IR，这不会使生成的 SB3 依赖 Linux。反过来，Linux 宿主并不需要 Windows 工具来构建编译器。

LLVM 预构建共享库可能依赖其发行版自己的运行库；本项目经 **C API** 使用它，不跨接口传递 libc++/libstdc++ 对象。默认项目标准库为 libc++，不要求 LLVM 本身也由同一 C++ 标准库构建。

## 2. 通用配置

准备工具链后，将相关 `bin` 放入 PATH，或通过环境变量/配置显式指定：

```sh
cmake --preset clang
cmake --build --preset clang --parallel 6
ctest --preset clang
```

`clang` preset 使用 Ninja、Release 和 `build/clang`。Windows 输出 `build/clang/scratch-llvm.exe`，Linux/macOS 输出 `build/clang/scratch-llvm`。切换宿主或工具链时使用新的构建目录，不复用原来的 Visual Studio 缓存。

也可以完全显式配置：

```sh
cmake -S . -B build/my-clang -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/toolchain/bin/clang \
  -DCMAKE_CXX_COMPILER=/toolchain/bin/clang++ \
  -DLLVM_ROOT=/llvm-prefix \
  -DSCRATCH_STDLIB_ROOT=/stdlib-prefix \
  -DSCRATCH_STDLIB=libc++ \
  -DSCRATCH_LINKER=lld
cmake --build build/my-clang --parallel 6
```

主要配置项：

| 变量 | 含义 |
| --- | --- |
| `CC` / `CXX` 或 `CMAKE_C_COMPILER` / `CMAKE_CXX_COMPILER` | 宿主 Clang 驱动 |
| `SCRATCH_TOOLCHAIN_ROOT` | 统一工具链前缀，帮助发现编译器和链接工具 |
| `LLVM_ROOT` | LLVM C API 安装前缀，可与宿主 Clang 前缀不同 |
| `LLVM_C_LIBRARY` | 链接用库：Windows 的 `.dll.a`/导入库，Linux/macOS 的共享库或相应链接库 |
| `LLVM_C_RUNTIME` | 可被 Python 加载的真实 `.dll`、`.so`、`.dylib`，不能是静态/导入库 |
| `LLVM_CONFIG_EXECUTABLE` | 可选的 llvm-config 22 路径 |
| `SCRATCH_STDLIB` | 默认 `libc++`；另有 `libstdc++` 选项，本次验证以 libc++ 为准 |
| `SCRATCH_STDLIB_ROOT` | 单独准备的标准库前缀，libc++ 头位于 `include/c++/v1`，库位于 `lib`、`lib/c++` 等 |
| `SCRATCH_LINKER` | `lld`、链接器的绝对路径，或显式 `default` |
| `SCRATCH_CLANG_EXECUTABLE` / `SCRATCH_CLANG` | 生成 guest 浮点 bitcode 的 Clang；应使用 LLVM 22 配套版本 |
| `SCRATCH_BUILD_FLOAT_RUNTIME` | 默认 ON；OFF 时运行浮点程序需要另提供 bitcode |

配置阶段会实际编译链接一个使用 C++17 filesystem 和异常的探针，及时发现标准库、ABI 库或链接器不完整的问题。

## 3. Windows：独立 SDK，无 Visual Studio

需要现有 Python 3.14 或更新版本，用于标准库自带的 `.tar.zst` 解包。准备脚本从官方 MSYS2 CLANG64 仓库下载锁定包，检查 SHA-256，只解包到指定前缀，不运行安装脚本、不要求管理员权限。

```powershell
python tools/bootstrap_windows.py
. ./build/toolchains/windows/activate.ps1
cmake --preset clang
cmake --build --preset clang --parallel 6
ctest --preset clang
```

SDK 包含 Clang 22.1.8、libc++/libc++abi、libunwind、compiler-rt、MinGW-w64 CRT/头文件、LLD、LLVM C API、Ninja 和原生 CMake。它不依赖 MSYS shell、Visual Studio 或 Windows SDK。

Windows 的系统 DLL 和系统 UCRT 属于操作系统接口，仍然需要；“独立 SDK”不意味着生成的 Windows 可执行文件不调用 Windows。

构建后会递归检查 PE 导入表，把实际需要的 SDK DLL 复制到编译器旁边。发现 MSVC C++ 或 MSYS 运行时依赖会报错，避免误用旧的工具链。详情及固定包列表见 [WINDOWS_TOOLCHAIN.md](WINDOWS_TOOLCHAIN.md)。

## 4. Linux：系统包或用户目录 SDK

可以直接使用发行版提供的 Clang/LLVM 22、libc++/libc++abi、LLD、Ninja 和 Python，或使用本项目的 Ubuntu 24.04 x86_64 准备脚本：

```sh
python3 tools/bootstrap_linux.py --with-node
. "$HOME/.cache/scratch-llvm/llvm-22.1.8-noble/activate.sh"
cmake --preset clang
cmake --build --preset clang --parallel 6
ctest --preset clang
```

该脚本下载并校验锁定的官方 `.deb`，在用户缓存目录解包，不修改 apt 源、不使用 sudo。它针对 Ubuntu 24.04 x86_64，不冒充所有 Linux 发行版的通用二进制安装器。其他系统通过 `LLVM_ROOT`、标准库及共享库路径接入。

Linux 的 libc++ 链接显式带上 libc++abi，并设置已配置依赖目录的 RPATH。发行版 glibc 等基础系统库仍由宿主提供。私有 SDK 中的 LLVM 二进制还可能依赖发行版 libstdc++6；这不改变本项目使用 Clang/libc++ 的构建选择。

WSL 下建议把构建目录和 Node 测试依赖放在 Linux 文件系统中；直接在 `/mnt/c` 上扫描大量 `node_modules` 可能显著拖慢加载。可用 `-B /home/.../build` 和 E2E 的 `--output-dir` 隔离产物。实际 Linux 验证记录见 [LINUX_VALIDATION.md](LINUX_VALIDATION.md)。

## 5. macOS 配置路径

以下使用 Homebrew 的版本化 LLVM 和单独的 LLD 配方；本次仅核对工具分发及配置路径，未在 macOS 运行：

```sh
brew install llvm@22 lld@22 cmake ninja python node
export LLVM_ROOT="$(brew --prefix llvm@22)"
export SCRATCH_STDLIB_ROOT="$LLVM_ROOT"
export CC="$LLVM_ROOT/bin/clang"
export CXX="$LLVM_ROOT/bin/clang++"
export PATH="$LLVM_ROOT/bin:$(brew --prefix lld@22)/bin:$PATH"
cmake --preset clang
cmake --build --preset clang --parallel 6
ctest --preset clang
```

需要兼容的 macOS SDK（系统 Command Line Tools，或自行配置的 SDK）。平台代码使用 Mach-O 的可执行路径查询；链接和安装使用 macOS RPATH 规则。Homebrew 将 LLD 单独分发，不能仅安装 Clang 就假定链接器和所有运行组件已经齐全。[Homebrew LLVM 22 说明](https://formulae.brew.sh/formula/llvm@22)

## 6. 安装与运行库定位

```sh
cmake --install build/clang --prefix /desired/prefix
```

默认布局：

```text
prefix/
  bin/scratch-llvm[.exe]
  bin/*.dll                 Windows 的实际运行依赖
  share/scratch-llvm/
    scratch-float.bc
    SoftFloat-LICENSE.txt
    stdlib/                 目标头文件、bitcode、manifest.json 和许可证
    debugger/               调试桥接及离线播放器
    tools/                  Python 工具
    libraries/              Console / Events / PTE / Triangle 源码包
    template/               可复制的项目模板，不含本机配置及构建产物
    licenses/
  bin/scrate[.cmd]           Python 启动入口及同目录配套模块
  share/doc/s_C_ratch__/     README、设计和 docs/
```

安装不包含 Clang、LLDB、Python、Node 或已经打包的 VSIX。将安装前缀的 `bin` 加入 PATH；完整新用户流程见 [快速入门](QUICKSTART.md)。

Linux/macOS 的宿主共享库需继续存在于系统或准备好的 SDK 中，安装 RPATH 会保留对应链接路径；目前不宣称把任意 Linux/macOS 动态库环境打成完全自包含包。

浮点运行库查找优先级：

1. `--float-runtime file.bc`
2. `--runtime-dir directory`
3. `SCRATCH_RUNTIME_DIR`
4. 真正的可执行文件所在目录
5. 相对于可执行文件的安装数据目录（默认 `../share/scratch-llvm`）

显式指定错误会直接报错，不静默退回别的运行库。许可证必须与所选 bitcode 同目录。通过 PATH、符号链接或从其他工作目录启动，都不再依赖项目根目录。

## 7. 验证方式

```sh
npm --prefix tests ci --ignore-scripts
cmake --preset clang
cmake --build --preset clang --parallel 6
ctest --preset clang
node tests/e2e.cjs --compiler build/clang/scratch-llvm \
  --output-dir build/clang/test-output/e2e --vm both --timeout 60000
```

Windows 最后一条使用 `.exe` 后缀。CMake 自动传正确的目标文件路径和独立测试输出目录；测试工具不再回退到旧 MSVC 可执行文件。

`tools/build_float_runtime.py --host-test` 使用所选 Clang 的真实默认 target 判断宿主 ABI，并支持显式链接器/sysroot。Python 3.12 与 MinGW 宿主的差异已经通过真实运行检查；这与生成 guest bitcode 是两条独立路径。

当前原版/TurboWarp 的程序语义支持范围保持不变。本次修改改变宿主构建和部署，不增加对大端、异常运行时或其他尚未支持 IR 特性的承诺。
