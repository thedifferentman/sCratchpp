# 跨平台改造验证记录

日期：2026-09-13。本记录对应 Clang 宿主构建与部署改造；此前 MSVC 首版记录保留在 [TEST_RESULTS.md](TEST_RESULTS.md)。

## 改动范围

- 统一 Clang GNU 风格驱动、libc++、LLD、Ninja preset，移除默认 Visual Studio 配置和系统 LLVM 路径猜测。
- 提供独立 Windows SDK 及 Ubuntu 用户目录 SDK 的准备脚本、固定包版本、SHA-256 校验和激活脚本。
- 把宿主可执行文件、宿主标准库/链接器、LLVM C API 和 guest bitcode 的目标配置分开。
- Windows UTF-8 命令行、Linux/macOS 可执行路径查询隔离在平台层；主逻辑不直接包含 Windows API。
- 支持 CMake 安装、项目外运行、PATH/符号链接定位、显式 runtime 目录和安装数据目录。
- 测试编译器发现和输出目录跨平台，避免复用旧 MSVC 可执行文件或互相覆盖测试产物。
- 修复 Python 3.12 FMA 参考、ELF `dso_local` 宿主测试副本及 MinGW freestanding DLL 入口差异。核心数值 C 源码和 LLVM→Scratch 语义实现未为跨平台而更换算法。

## 实际验证结果

| 项目 | Windows x86_64 | Linux x86_64 |
| --- | --- | --- |
| 宿主环境 | 原生 Windows，私有 CLANG64 SDK | Ubuntu 24.04 / WSL2，私有 LLVM SDK |
| 编译器/链接器 | Clang 22.1.8、LLD 22.1.8 | Clang 22.1.8、LLD 22.1.8 |
| 项目标准库 | libc++，MinGW/UCRT ABI | libc++＋libc++abi |
| 构建 | Ninja Release，独立 CMake 4.4.3 | Ninja Release，Ubuntu CMake 3.28.3 |
| CTest | 11/11，包含完整 E2E | 9/9 非 VM 项；VM 项在 ext4 副本单独执行 |
| E2E fixtures | 50/50 | 50/50 |
| 正例执行 | 40 正例×原版/TurboWarp＝80 次 | 40 正例×原版/TurboWarp＝80 次 |
| 诊断负例 | 10/10 | 10/10 |
| 软件浮点原生参考 | 66,344 项通过 | 66,344 项通过，Python 3.12 |
| SysV 变参原生参考 | 两例结果 58、57 | 两例结果 58、57 |
| 安装后运行 | 清除 SDK PATH 后，从 TEMP 通过 PATH 编译浮点程序成功 | 删除 LD_LIBRARY_PATH 后，从 /tmp 通过绝对路径、PATH、中文符号链接编译成功 |
| 无效显式运行库配置 | 明确报错，不静默回退 | 平台层及安装测试通过 |

Windows 配置时清除了 `INCLUDE/LIB/LIBPATH` 等 VS 环境变量。SDK 的 Clang、LLD、Ninja、CMake 和库由单独前缀提供；最终程序的 PE 依赖闭包包含程序自身及 7 个 SDK DLL，没有 MSVC C++ 或 MSYS 运行时。操作系统 DLL/UCRT 仍由 Windows 提供。

Linux 执行的是实际 ELF 二进制，不是借助 Windows 程序运行编译步骤。项目动态链接 libc++/libc++abi，发行版 LLVM 共享库自身仍有其系统运行依赖。Linux 详细记录见 [LINUX_VALIDATION.md](LINUX_VALIDATION.md)。

## 构建身份

Windows 编译器：`build/clang/scratch-llvm.exe`。

```text
4244494a276c08956af2ddfbd2b43c73a39fc8b4f4b7c151078d9c2b28b0f096
```

Linux 编译器的工作区副本：`build/linux/scratch-llvm`。

```text
df5c982dfac3f1cc072c700115186d722e8c6740b7160cb935e64a8f2d3b52c9
```

两者均与各自完整 E2E 报告中的 SHA-256 一致。Linux 最后一次 RPATH 配置调整导致重新链接，因此已使用最终二进制重跑 E2E，而不是沿用旧哈希的结果。

原始记录位于工作区构建目录：

- `build/clang/test-output/e2e/report.json`
- `build/clang/Testing/Temporary/LastTest.log`
- `build/validation/linux/e2e-report.json`
- `build/validation/linux/ctest-final.log`
- `build/validation/linux/final-build-and-ctest.log`
- `build/validation/linux/install-final-commands.json`
- `build/toolchains/windows/verification.json`

## 边界

macOS 平台代码、依赖发现和安装规则已准备，构建说明给出了 LLVM 22/独立 LLD/SDK 路径；本次没有 macOS 主机，未宣称 macOS 已实机通过。

Windows SDK bootstrap 只面向 Windows x86_64，Ubuntu bootstrap 只面向 Ubuntu 24.04 x86_64。其他宿主和架构使用通用 CMake 配置与其对应工具链，不能直接使用另一平台的预构建库。

原版 Scratch/TurboWarp 测试仍为计算与资源加载验证；没有据此宣称绘图像素、所有外部库或完整 Clang 自举已经通过。LLVM IR 支持范围保持 [README](../README.md) 中的实际范围。
