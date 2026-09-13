# C++ → Scratch：VS Code 模板

复制此目录即可作为独立工程。默认示例绘制方形螺旋，产物为 `build/project.sb3`。

## 开始使用

1. 准备 Python 3.9+、Clang 22.1，以及已经构建或安装的 `scratch-llvm` 和目标标准库 SDK。编译器的动态库、`scratch-float.bc`、`stdlib/` 和许可证保持原有安装布局，不要只复制一个可执行文件。仓库默认 CMake 构建会生成 `build/clang/stdlib/`。
2. 在 VS Code 中**打开本目录**，或打开复制后的目录。安装 Microsoft C/C++ 扩展用于补全和错误提示；构建本身不依赖扩展。运行和调试还需要安装下面介绍的 Scratch LLVM Debugger 扩展。
3. 如果 `clang++` 和 `scratch-llvm` 已在 `PATH` 中，无需配置。否则将 `toolchain.example.json` 复制为 `toolchain.local.json`，填写两个可执行文件的路径。相对路径以本目录为基准，JSON 中 Windows 路径建议使用 `/`。
4. 执行 **终端 → 运行生成任务**（Windows/Linux：`Ctrl+Shift+B`；macOS：`Cmd+Shift+B`）。
5. 在 Scratch 或 TurboWarp 中从电脑上传 `build/project.sb3`，点击绿旗运行。

若直接使用编译器仓库中的 Windows 独立 SDK，且模板仍位于原来的 `template/`，本机配置可以写成：

```json
{
    "clang": "../build/toolchains/windows/root/clang64/bin/clang++.exe",
    "scratch_llvm": "../build/clang/scratch-llvm.exe"
}
```

复制模板到其他位置后应改成实际安装路径。Linux/macOS 填对应平台的可执行文件路径，构建脚本不依赖 PowerShell、Visual Studio 或 Windows SDK。

标准库会从编译器旁的 `stdlib/`、安装位置的 `../share/scratch-llvm/stdlib/`，或模板相邻的 `../build/stdlib/` 查找。也可在本机配置中加 `"stdlib": "/path/to/stdlib"`；该路径应直接包含 `manifest.json`。这是面向 Scratch 编译的 SDK，不是宿主 libc++ 目录。

单独准备 SDK（在编译器仓库根目录执行）：

```sh
python3 tools/build_stdlib.py --clang /path/to/clang --output-dir build/stdlib
```

也可以通过环境变量 `SCRATCH_CLANG`、`SCRATCH_LLVM`、`SCRATCH_STDLIB_DIR` 指定工具和 SDK 路径；环境变量优先于本机配置。`SCRATCH_RUNTIME_DIR` 可指定浮点运行时目录。修改环境变量后重新启动 VS Code，确保构建进程继承新值。

## 编写代码

- 入口使用普通 `int main()`。示例用 `std::vector` 和 `std::sort` 准备绘图数据。
- 所有 `src/` 下的 `.cpp`（包括子目录）自动编译、统一在 LLVM 层链接，无需手动维护文件列表。同名源文件放在不同子目录也可以。
- 公共头文件放在 `include/`。`scratch.hpp` 提供几个使用官方 opcode 的整数画笔/移动接口，可按相同方式扩展。
- 每次构建完整编译当前源文件，所以删除源文件或修改头文件都会正确生效。
- 构建会生成 `build/compile_commands.json`，C/C++ 扩展据此使用实际编译参数。第一次构建前，若 Clang 不在 `PATH`，可将 `.vscode/c_cpp_properties.json` 的 `compilerPath` 改为本机路径；新源文件构建后也会加入编译数据库。

命令行同样可用，且可从任意工作目录调用：

```sh
python3 build.py
```

Windows 默认用 `python build.py`。如果 Python 命令名不同，修改 `.vscode/tasks.json` 中的 `command` / `windows.command` 即可。

## 运行和调试

在 VS Code 的扩展菜单中选择 **从 VSIX 安装**，安装编译器仓库提供的 `debugger/dist/scratch-llvm-debugger-0.1.1.vsix`。扩展 ID 为 `scratch-llvm.debugger`，不要求安装 TurboWarp Debugger 插件。也可执行：

```sh
code --install-extension /path/to/scratch-llvm/debugger/dist/scratch-llvm-debugger-0.1.1.vsix
```

调试使用真正的 LLDB：我们的桥接提供 Scratch 虚拟目标、执行控制和 DWARF 符号，LLDB 负责源码位置、调用栈和基本变量的调试接口。因此还需要安装包含 `lldb-dap` 的 LLVM（旧版可用 `lldb-vscode`）；LLDB 与用于生成 bitcode 的 Clang 可以分开配置。扩展的打包及连接说明见编译器仓库的 `debugger/README.md`。

在仓库根目录可运行以下命令检查 LLDB-DAP 及其 Python 依赖：

```sh
python3 tools/bootstrap_debugger.py --lldb-dap /path/to/lldb-dap
```

部分 Windows x64 LLVM 发行版需要 `python311.dll`。如果机器只有其他 Python 版本，可明确执行以下准备命令；它只向仓库 `build/debugger/python311/` 下载并解压官方 Python 3.11.9 嵌入包，不修改系统 PATH、不安装 Visual Studio：

```sh
python tools/bootstrap_debugger.py --lldb-dap "C:/Program Files/LLVM/bin/lldb-dap.exe" --prepare-python
```

下载使用固定 SHA256 校验，随后验证 LLDB 的 Python 标准库和 DAP 初始化。该命令仅适用于依赖 Python 3.11 的 Windows x64 LLDB；Linux/macOS 使用其 LLDB 发行版配套的 Python，辅助脚本只做诊断。开始调试时不会自动联网安装依赖。

在 `toolchain.local.json` 中可增加 `lldb_dap` 和 `lldb_python`，路径相对于项目目录。例如：

```json
{
    "clang": "../build/toolchains/windows/root/clang64/bin/clang++.exe",
    "scratch_llvm": "../build/clang/scratch-llvm.exe",
    "lldb_dap": "C:/Program Files/LLVM/bin/lldb-dap.exe",
    "lldb_python": "../build/debugger/python311"
}
```

也可在 `launch.json` 使用对应的 `lldbDap`、`lldbPython` 字段。私有 Python 仅添加到调试子进程的 DLL 搜索路径，不影响用于构建模板的 Python 命令。

在“运行和调试”下拉框选择：

- **Scratch: Debug**：F5 先执行 **Scratch: Debug** 任务，然后启动 LLDB-DAP 和 Scratch 调试桥接，连接 TurboWarp。使用源码断点、继续、暂停、单步进入／跳过／跳出。
- **Scratch: Run in TurboWarp**：先执行常规构建，再直接打开 TurboWarp 运行产物。此配置设置 `mode: "run"`；`noDebug` 是 VS Code 发给适配器的协议字段，不需要写入 launch.json。

默认使用浏览器中的 TurboWarp。直接运行时若要用桌面应用，可在 Run 配置中加 `"turbowarp": "/path/to/TurboWarp"`；Windows 路径同样可用 `/`。调试首版使用浏览器解释器，`turbowarpUrl` 可以指定编辑器网页地址。打开页面后若 TurboWarp 提示加载本地调试扩展，按提示允许；源码和 SB3 由本地桥接提供。具体连接过程以调试器说明为准。

也可以只构建调试产物：

```sh
python3 build.py --debug
```

Debug 产物为 `build/debug/project.sb3` 和 `build/debug/project.debug.json`。用户 C++ 代码使用 `-O0 -g -fstandalone-debug`，保留源码位置；LLVM 链接阶段仍启用 `--whole-program` 裁剪，但不执行完整 O2。预编译标准库保持 SDK 的优化设置。调试运行关闭 TW 编译；这些设置不改变普通构建的 O2 流水线。

调试映射必须与 SB3 配套，不要单独替换其中一个文件。调试构建会同时更新 `build/debug/compile_commands.json` 和供 IntelliSense 使用的 `build/compile_commands.json`；常规构建后 IntelliSense 切回常规编译参数。两种模式的 bitcode 和 SB3 分目录保存，互不覆盖。

## 编译范围

模板默认启用 Clang `-O2`、链接后 `default<O2>` 和 `--whole-program` 裁剪。编译器根据实际需要自动保留 `main`、后续浮点降级所需的辅助入口及 LLVM 特殊根，再删除未使用的库代码；不要另加只保留 main 的 `internalize` pass，否则可能提前删除浮点辅助函数。

普通 C++ `float`/`double` 运算可以直接使用。需要精确浮点运行库时，编译器自动从自身安装布局找到 `scratch-float.bc` 和 `SoftFloat-LICENSE.txt`，并把许可证放入 `.sb3`。可用 `SCRATCH_RUNTIME_DIR` 覆盖该目录；这与 `SCRATCH_STDLIB_DIR` 指向的 C++ SDK 是两个不同位置。

`examples/floating.cpp` 是小型浮点绘图示例，复制到 `src/main.cpp` 后按常规方式构建。当前 `scratch.hpp` 的画笔桥接参数仍为整数，需要时显式转换；浮点计算本身保留正常类型与精度语义。这里支持的是已实现的浮点运算和 intrinsic，不等同于已经提供完整 `<cmath>` 的普通函数运行库。

流程为 `C++ → Clang LLVM bitcode → scratch-llvm → .sb3`，固定使用 `x86_64-unknown-linux-gnu` 的小端布局，保证不同宿主生成一致的目标 ABI；**运行产物不需要 Linux**。模板只生成 bitcode，不调用宿主机器链接器。

默认 C++17、`-O2`，禁用异常、RTTI、线程及栈保护；局部静态对象使用单执行上下文的初始化保护。目标 SDK 基于 libc++ 22.1.8，提供第一批容器、算法、基础字符串、内存分配和对象生命周期支持。错误路径按禁用异常的配置终止程序，不能捕获异常。

分配器默认有 16 KiB 可复用堆，与其他静态数据、栈一起位于虚拟字节内存中；不是宿主堆。`iostream`、`printf`、完整数学库、locale、文件系统、线程及 RTTI 尚未接入。后续输入将使用询问，输出使用独立 Console 列表。本次先提供第一批底层能力，不自动实现这些后续接口。

SDK 的头文件和实现必须一起使用；不要改成宿主头文件或链接本机 `.lib/.so/.dll`。构建数据库会记录实际目标头文件路径，首次构建后代码补全使用相同配置。生成的 `.sb3` 包含 libc++ 许可证。

调试使用 LLVM 源码信息、Scratch 执行点映射、TW 解释器桥接及真正的 LLDB；容器美化查看暂未实现。LLDB 面对的是 Scratch 虚拟执行目标，程序并不会作为宿主机器码执行。

VS Code 配置格式参考：[Tasks](https://code.visualstudio.com/docs/debugtest/tasks)、[C/C++ 配置](https://code.visualstudio.com/docs/cpp/customize-cpp-settings)。
