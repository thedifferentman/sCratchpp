# Linux 工具链与验证记录

本记录针对原生 Linux ELF 构建。WSL 仅用于提供本次验证机器，构建命令及工具链不调用 Windows 编译器、Windows SDK、PowerShell 或 Windows 可执行文件。

## 可复现的工具链

本次使用 Ubuntu 24.04 x86_64。仓库提供可选脚本，将官方预编译包解包到当前用户目录，不使用 `sudo`，不修改系统 apt 源或安装数据库：

```sh
python3 tools/bootstrap_linux.py --with-node
. "$HOME/.cache/scratch-llvm/llvm-22.1.8-noble/activate.sh"
```

脚本仅面向 Ubuntu 24.04 x86_64。其他发行版及架构使用对应平台的 LLVM 包，通过项目的 `LLVM_ROOT`、编译器和库路径选项接入。

下载版本及每个文件的 SHA256 固定在 [linux-toolchain.lock.json](../tools/linux-toolchain.lock.json)：

| 组件 | 本次版本与来源 |
|---|---|
| Clang、LLVM 共享库、LLVM 链接工具、LLD | 22.1.8，LLVM 官方 [apt.llvm.org Noble 仓库](https://apt.llvm.org/noble/) |
| libc++、libc++abi、libunwind、compiler-rt | 同一 LLVM 22.1.8 仓库构建 |
| Node.js | 22.22.0，[Node.js 官方发行目录](https://nodejs.org/dist/v22.22.0/) |
| CMake、Ninja、Python | Ubuntu 主机提供 |
| C 标准库、启动对象、系统头文件 | Ubuntu glibc 开发环境提供 |

这些是宿主编译工具及标准库，不是供 Scratch 虚拟机运行的目标 C/C++ 标准库。LLVM 官方 Linux 二进制本身仍有 glibc、libstdc++、libffi、zlib、zstd、libxml2 等系统依赖；项目的 C++ 源码可以独立使用 libc++ 编译。

私有环境同时导出 `CC`、`CXX`、`LLVM_ROOT`、`SCRATCH_STDLIB_ROOT`、`SCRATCH_CLANG`、`SCRATCH_LLVM_LIBRARY`、`PATH` 和 `LD_LIBRARY_PATH`。所有变量只影响激活它的 shell。脚本在私有 `LLVM_ROOT/lib` 建立 LLVM C API 的相对符号链接，便于 CMake 发现共享库，无需下载完整静态 LLVM 开发包。

bootstrap 最后实际编译、链接并运行一个使用 `std::string` 和 `std::cout` 的程序，使用：

```text
clang++ -std=c++17 -stdlib=libc++ -fuse-ld=lld
        --rtlib=compiler-rt --unwindlib=libunwind ... -lc++abi
```

Noble 的此版本 libc++ 包需要显式链接 `libc++abi`；仅使用 `-stdlib=libc++` 不足以提供所有 C++ ABI 符号。

## 构建与安装命令

在仓库目录激活上述工具链后，本次使用如下配置。构建目录放在 Linux 文件系统中，源目录可以使用任意路径：

```sh
cmake -S "$PWD" -B "$HOME/.cache/scratch-llvm/linux-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$LLVM_ROOT/bin/clang" \
  -DCMAKE_CXX_COMPILER="$LLVM_ROOT/bin/clang++" \
  -DLLVM_ROOT="$LLVM_ROOT" -DSCRATCH_STDLIB_ROOT="$LLVM_ROOT" \
  -DLLVM_C_LIBRARY="$SCRATCH_LLVM_LIBRARY" \
  -DLLVM_C_RUNTIME="$SCRATCH_LLVM_LIBRARY"
cmake --build "$HOME/.cache/scratch-llvm/linux-build" --parallel 8
ctest --test-dir "$HOME/.cache/scratch-llvm/linux-build" \
  --output-on-failure -E '^(e2e|assembly-vm)$'
cmake --install "$HOME/.cache/scratch-llvm/linux-build" \
  --prefix "$HOME/.cache/scratch-llvm/linux-install"
```

上述 CTest 排除的两个 VM 测试在下一节描述的 ext4 测试副本中单独执行。普通 Linux checkout 直接位于原生文件系统时，可以运行完整 CTest，无需复制。

安装树将执行文件放在 `bin`，将目标浮点 bitcode 及许可证放在 `share/scratch-llvm`。它通过相对安装布局寻找 bitcode，不依赖当前工作目录。Linux 的 LLVM/libc++ 共享库仍由准备好的工具链提供；本次安装记录并不表示已经生成不依赖系统库的独立发行包。

## 验证环境注意事项

本次 WSL 从 NTFS 挂载目录直接加载数千个 Node 模块时，Scratch VM 的加载超过了测试框架 30 秒的独立加载时限。Linux 运行测试时将测试脚本、fixtures 和同一锁定版本的纯 JavaScript 依赖复制到 WSL 的 ext4 用户缓存中，以避免文件系统桥接开销；不扩大程序执行时限。

Linux 和 Windows 测试输出使用独立目录，避免互相覆盖 SB3、原生共享库和报告。编译器产物及测试依赖无需复制到系统目录。

## 已执行检查

| 检查 | 结果 |
|---|---|
| bootstrap 首次执行、缓存重复执行 | 均通过；所有下载按锁文件 SHA256 验证 |
| Clang/LLD/llvm-link/llvm-ar 版本 | 均为 22.1.8 |
| libc++ / libc++abi / compiler-rt / libunwind 链接运行 | 通过，输出 `libc++ OK` |
| 平台层独立 Clang 编译 | `-Wall -Wextra -Wpedantic -Werror` 通过 |
| 平台层运行 | 从 `/tmp` 工作目录运行通过，覆盖 UTF-8 参数 `é 中文 "quoted"`、路径身份和运行库发现 |
| `node --test tests/compiler_path.test.cjs` | 9/9 通过 |
| `node --test tests/vm_runner.test.cjs` | 5/5 通过，使用真实 Scratch 和 TurboWarp VM，总计约 3 秒 |
| 完整 Clang + libc++ + LLD Release 构建 | 构建完成，包含 SoftFloat bitcode 生成；最终配置重建零警告 |
| CTest，排除单独运行的 VM 项 | 9/9 通过；最终 CMake、平台参数处理及链接配置复验仍为 9/9，约 10.6 秒 |
| 完整 E2E | 最终编译器重新运行 50/50 fixtures 通过：40 个正例在两个 VM 共执行 80 次，10 个负例正确拒绝，约 139.5 秒 |
| `assembly_vm.cjs` | 两个 VM 各 17 个数值用例、1 个画笔加载及执行用例通过；未验证像素渲染 |
| 安装布局 | 从 `/tmp` 通过绝对路径、PATH 及含中文空格的符号链接调用，3 次浮点程序编译均通过 |
| 无环境运行 | 上述安装检查删除 `LD_LIBRARY_PATH` 与 `SCRATCH_RUNTIME_DIR`，由 ELF 运行路径解析宿主共享库、相对安装布局解析 bitcode |
| ELF 检查 | `.comment` 确认 Clang 22.1.8 / LLD 22.1.8；`ldd` 确认 libc++ / libc++abi / LLVM 22 |
| SoftFloat 原生参考测试 | Python 3.12 / ELF 共享库下 66,344 项精确有理数 oracle 检查通过，包含 FMA |
| SysV 变参原生参考 | `varargs_ir = 58`、`varargs_sysv = 57`，两项均通过 |

本次发现并修复两个测试工具的宿主假设：Python 3.12 没有 `math.fma`，因此特殊 FMA 也使用精确参考规则；原有 IR 的 `dso_local` 标记不能仅靠 `-fPIC` 改为共享库所需的可抢占引用，因此仅在宿主对照副本中调整该标记，保留被测试的原始 IR。

原始 E2E 报告、CTest 日志、安装调用参数及 CMake 缓存保存在 `build/validation/linux/`。已将 ELF 编译器、bitcode 和 SoftFloat 许可证复制到 `build/linux/`，便于在本机 Linux 环境继续使用；宿主动态库按上文说明另行提供。

最终 E2E 报告的编译器 SHA256 为 `df5c982dfac3f1cc072c700115186d722e8c6740b7160cb935e64a8f2d3b52c9`，已与构建目录及 `build/linux/` 中的实际二进制复核一致。报告也记录并核对了 fixtures manifest 的 SHA256。

最终配置重新运行 CMake、重建目标并完成非 VM CTest。由于运行库搜索路径更新改变了 ELF 标识，完整 E2E 也重新执行，确保报告对应最终二进制。最终 CTest 日志为 `build/validation/linux/ctest-final.log`；最终完整 E2E 报告为 `build/validation/linux/e2e-report.json`，首次报告保留为 `e2e-initial-report.json`。安装后的绝对路径、PATH 及符号链接浮点编译检查也已对最终版本复验。
