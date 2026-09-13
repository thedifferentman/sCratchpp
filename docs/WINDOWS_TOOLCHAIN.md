# 独立 Windows Clang 工具链

Windows 宿主使用原生 Clang GNU driver、LLVM 的 C++ 标准库和 LLD，不需要
Visual Studio、MSVC 编译器、Windows SDK 安装，也不需要 MSYS shell 或 pacman。
此工具链只解决 Windows 宿主构建；Scratch 输入模块的目标 ABI 独立选择，不能
把 Windows Clang 的默认目标当作 Scratch 目标。

## 准备

需要 Windows 10/11 x86_64 和 Python 3.14 或更新版本。Python 的标准库直接解压
Zstandard 包，不需要额外 Python 包。第一次需要下载约 226 MB，解包、链接
副本和缓存合计应预留至少 2.5 GB 空间。

在项目根目录运行：

```powershell
python tools/bootstrap_windows.py
. ./build/toolchains/windows/activate.ps1
clang --version
cmake --version
ninja --version
```

也可以通过 `--prefix <路径>` 指定私有工具链目录。脚本不会修改系统 PATH，
不执行包内 `.INSTALL` 等脚本；激活文件只修改当前 shell 的环境变量。
在 cmd 中可使用生成的 `activate.cmd`。

所有包版本、完整下载地址、SHA-256 和依赖记录在
[`tools/toolchain-lock-windows.json`](../tools/toolchain-lock-windows.json)。
脚本只使用锁定文件，不在运行时选择最新包。已下载的包会重新校验摘要后复用。
归档路径和链接目标必须留在工具链提取目录内；归档链接复制为普通文件，
无需管理员权限或 Windows 开发者模式。

## 组件与布局

| 组件 | 锁定版本 / 实现 |
|---|---|
| Clang、LLVM、LLD、compiler-rt | 22.1.8，MSYS2 包修订 2 |
| libc++、libc++abi、libunwind | LLVM 22.1.8 |
| Windows C 头文件、启动对象、导入库 | mingw-w64 CLANG64 CRT / headers |
| C++ 标准库 | libc++，不使用 MSVC STL |
| CMake | 4.4.3，独立原生包 |
| Ninja | 1.13.2 |
| LLVM C API 动态库 | `bin/libLLVM-22.dll` |
| LLVM C API 导入库 | `lib/libLLVM-22.dll.a` |
| LLVM C API 头文件 | `include/llvm-c` |

默认目录如下：

```text
build/toolchains/windows/
  downloads/                    校验过的包缓存
  root/clang64/bin/              clang、lld、cmake、ninja、运行时 DLL
  root/clang64/include/          C / C++ / LLVM 头文件
  root/clang64/lib/              标准库、CRT、LLVM 导入库
  activate.ps1                   PowerShell 当前会话环境
  activate.cmd                   cmd 当前会话环境
  clang-toolchain.cmake          原生 Clang / libc++ / LLD 配置
  toolchain.json                 供其他脚本读取的绝对路径
  verification.json              实际编译执行结果
  verify/sdk_probe.cpp           验证源码
  verify/sdk_probe.exe           验证程序
```

`activate.ps1` 设置 `CC`、`CXX`、`SCRATCH_CLANG`、
`SCRATCH_LLVM_LIBRARY`、`SCRATCH_WINDOWS_SDK`、`LLVM_ROOT`、
`SCRATCH_TOOLCHAIN_ROOT`、`SCRATCH_STDLIB_ROOT`，并将 SDK 的 `bin` 加入 PATH。
移动已生成的工具链目录后，应重新运行脚本，刷新这些绝对路径。

## 构建项目

必须使用新的构建目录，不能继续使用旧 MSVC 构建目录中的缓存和目标文件。
激活后使用 SDK 自带的 CMake 和 Ninja，项目 preset 会读取上述环境变量：

```powershell
. ./build/toolchains/windows/activate.ps1
cmake --preset clang
cmake --build --preset clang
ctest --preset clang
```

这会在 `build/clang` 中构建。若需要独立命名的构建目录，也可显式指定：

```powershell
cmake -S . -B build/clang-windows -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=build/toolchains/windows/clang-toolchain.cmake `
  -DCMAKE_BUILD_TYPE=Release `
  "-DLLVM_ROOT=$env:SCRATCH_WINDOWS_SDK" `
  "-DLLVM_C_LIBRARY=$env:SCRATCH_WINDOWS_SDK/lib/libLLVM-22.dll.a" `
  "-DLLVM_C_INCLUDE_DIR=$env:SCRATCH_WINDOWS_SDK/include" `
  "-DSCRATCH_CLANG_EXECUTABLE=$env:SCRATCH_CLANG"
cmake --build build/clang-windows
ctest --test-dir build/clang-windows --output-on-failure
```

配置明确使用 `-stdlib=libc++` 和 `-fuse-ld=lld`。LLVM C API 也来自同一套
CLANG64 包，不能混用原系统 `LLVM-C.dll` 或 MSVC `.lib`。

构建工具需要 SDK 的 `bin` 在 PATH 中。项目构建会把编译器实际依赖的 DLL
复制到可执行文件旁边；发布时仍须一并携带这些 DLL 和对应许可证。
本脚本准备的是完整开发 SDK，不是最小发布目录。

## 验证范围

脚本默认清除 `INCLUDE`、`LIB`、`LIBPATH`、`CL` 等开发环境变量，并把验证
子进程的 PATH 限制为 SDK `bin` 和 Windows 系统目录。随后执行：

1. 检查 Clang、LLD、独立 CMake、Ninja 的版本。
2. 使用 Clang GNU driver 编译并链接 C++17 `std::filesystem`、C++ 异常及
   LLVM C API 探针，显式使用 libc++ 和 LLD。
3. 执行探针，创建和销毁 LLVM context/module，并验证异常捕获。
4. 递归检查探针和开发工具的 PE 导入表，记录实际依赖，并拒绝 MSVC C++
   运行时或 MSYS 运行时依赖。

`--skip-verify` 仅提取文件，不代表工具链通过验证。

这不要求安装 Visual Studio 或 Windows SDK，但原生 Windows 程序仍依赖
操作系统 DLL 和随现代 Windows 提供的 UCRT。这里的“独立”指开发工具和
头文件/链接库由私有 SDK 提供，不是把 Windows 系统 API 一并模拟掉。

## 来源与维护

选用的是 [MSYS2 CLANG64 官方环境](https://www.msys2.org/docs/environments/)
的原生包，该环境采用 LLVM、LLD、libc++ 和 UCRT。LLVM 动态库来自
[官方 llvm-libs 包](https://packages.msys2.org/packages/mingw-w64-clang-x86_64-llvm-libs)，
不是系统已安装的 MSVC 版本。

锁定文件依据官方 `clang64.db` 的运行时依赖递归生成，保留索引本身的摘要。
只纳入 `DEPENDS` 及所需工具，未纳入编译 MSYS2 包本身的 `MAKEDEPENDS`。
虚拟依赖 `cc-libs` 由 libc++ 提供。更新锁定文件时应在新的空前缀重新解包，
重复 SDK 探针及项目构建测试，避免不同版本残留文件混用。

官方镜像不保证无限期保存旧版本归档。长期复现应保留 `downloads` 缓存，
或按原 SHA-256 将归档保存到可信的项目制品仓库；不得静默换用新版包。
