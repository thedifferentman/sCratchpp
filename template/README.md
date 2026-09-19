# C++ → Scratch：VS Code 模板

复制此目录即可作为独立工程。当前示例使用控制台包，产物为 `build/project.sb3`。

## 开始使用

1. 安装 scrate 并确认 `scrate --version` 可用；准备 Python 3.11+、Clang 22.1，以及已经构建或安装的 `scratch-llvm` 和目标标准库 SDK。scrate 及配套 Python 模块、编译器的动态库、`scratch-float.bc`、`stdlib/`、资源工具和许可证保持原有安装布局，不要只复制一个可执行文件。仓库默认 CMake 构建会生成 `build/clang/stdlib/`。
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

- 入口使用普通 `int main()`。当前示例用 `std::cin` 读取两个整数，再用 `std::cout` 输出它们的和。
- 所有 `src/` 下的 `.cpp`（包括子目录）自动编译、统一在 LLVM 层链接，无需手动维护文件列表。同名源文件放在不同子目录也可以。
- 公共头文件放在 `include/`。`scratch.hpp` 声明整数画笔/移动、造型切换和显示接口；构建时根据 `sCrpp.toml` 生成 `build/generated/scratch.cpp` 实现并一起链接。
- 每次构建完整编译当前源文件，所以删除源文件或修改头文件都会正确生效。
- 构建会生成 `build/compile_commands.json`，C/C++ 扩展据此使用实际编译参数。第一次构建前，若 Clang 不在 `PATH`，可将 `.vscode/c_cpp_properties.json` 的 `compilerPath` 改为本机路径；新源文件构建后也会加入编译数据库。

在项目目录可以直接使用已安装的命令：

```sh
scrate build
```

Windows、Linux、macOS 都使用 `scrate build`。从其他目录调用时加 `--manifest /path/to/sCrpp.toml`。模板不再包含 Python 启动脚本。

## 图片与造型资源

sCr++ 只有一个执行角色 `Program`。资源包只用于命名和链接，所有造型都挂在该角色上，不生成多个角色。

项目根目录的 `sCrpp.toml` 声明资源：

```toml
version = 1

[package]
name = "example"

[[costumes]]
file = "marker.svg"            # 查找 resources/marker.svg，默认名 marker

# [[costumes]]
# file = "images/background.png" # 相对于本清单，而不是 resources/
# name = "background"           # 可选，默认文件名去掉扩展名
# size = [480, 270]             # 可选，最终造型逻辑尺寸
# center = [240, 135]           # 可选，最终造型坐标；默认画布中心

# [link]
# resources = ["../shared/sCrpp.toml"] # 独立资源包，可以继续引用其他包
```

只写文件名时从清单旁的 `resources/` 查找；含目录的显式路径相对于清单。路径可包含空格和中文，包名和造型名也支持 Unicode，但不能包含分隔符 `::`。同一文件内容可以复用；重复的包名、完整造型名或循环资源引用会报错。首版打包所有声明的资源，不做资源裁剪。

PNG 保留原始像素数据并嵌入 SVG；默认逻辑尺寸等于原始像素尺寸，不自动缩放到舞台。`size` 只改变显示尺寸，不缩小嵌入图片。SVG 同样支持 `size`，中心点均按最终逻辑坐标填写。高清资源不代表原版 Scratch 的画笔层也支持高清图章。

```cpp
#include "scratch.hpp"

int main() {
    scratch::set_costume("marker"); // 当前清单的 example::marker
    scratch::go_to(0, 0);
    scratch::show();
    scratch::stamp();
    scratch::hide();              // 已盖下的图章保留；clear() 清除画笔层
}
```

`set_costume_qualified("shared::button")` 接受完整名称，用于访问独立链接的资源包。名称在运行时由普通 `std::string` 构造，造型不存在时沿用 Scratch 行为：保持旧造型，之后盖章也会盖出旧造型。可以把 `examples/resources.cpp` 复制到 `src/main.cpp` 体验。

`scratch::set_string(const std::string&)` 单独将 UTF-8 文本写入 Scratch 原生变量 `__scl_string`。造型包装先调用该函数，再执行官方造型切换积木。该变量由所有调用共用，下一次写入会覆盖旧文本；本项目的单执行上下文允许这种用法。它不提供标准输入输出。

资源处理使用 scrate 安装中的 `build_resources.py`。旧 `resource_builder` 配置仅为兼容而接受，构建不执行项目指定的 Python 资源脚本。生成的 C++、资源清单和图片全部位于 `build/`（调试构建位于 `build/debug/`），不要编辑生成文件；它们也会加入实际编译数据库。没有 `sCrpp.toml` 时仍能构建旧项目，此时 `set_costume` 按完整名称切换、不加包名前缀。

## 使用 scrate 包

基础 SDK 只自动提供 C++ 标准库和底层运行时。Console、Events、PTE 都是普通包，
通过 `sCrpp.toml` 的 `[dependencies]` 声明，由 scrate 解析、校验并生成 `scrate.lock`。

```toml
[dependencies]
console = "0.3.0"
```

默认仓库为 `https://scrate.shapy.cn`。模板使用已发布的 Console 0.3.0，首次构建联网下载，
自动带入 Events 0.1.0、PTE 0.1.0 和 Triangle 0.1.1，不依赖编译器仓库旁的 include 目录。
如需开发本地包，可显式使用 `{ version = "0.3.0", path = "/path/to/console" }`。

构建命令为 `scrate build`。首次下载后，使用 `scrate build --offline --locked`
可按锁文件离线重建；请提交 `scrate.lock`。本地包内容变化时先普通构建更新锁文件。
`toolchain.local.json` 可指定 `registry`、`package_cache`；
对应环境变量为 `SCRATE_REGISTRY`、`SCRATE_CACHE_DIR`。

模板支持源码包和声明匹配 LLVM 版本、目标及 sCr++ ABI 的 bitcode 包。只有实际声明的包资源参与链接。
无依赖项目不会携带字库或事件帽子。旧 `[link] resources` 仍用于本地资源链；带包依赖的库应改为 `[dependencies]`。
运行一次构建会刷新 `build/compile_commands.json`，VS Code 从中读取 SDK 与包的头文件路径。

复制 `examples/console.cpp` 到 `src/main.cpp` 即可构建可滚动示例：它输出 60 行编号文本后持续刷新，可用滚轮查看历史，点击 Scratch 停止按钮退出。最小的一次性输出则是：

```cpp
#include <console/console.hpp>

int main() {
    scratch::console::init();
    scratch::console::writeln("Hello from C++!");
    scratch::console::flush();
}
```

Console 使用唯一 `Program` 角色和画笔层绘制文字。`write`/`writeln` 更新内容，`flush()` 绘制待更新内容并处理滚轮输入；交互程序应在自己的循环里反复调用 `flush()`。程序返回后画面保留，但不再由主循环处理新的滚轮输入。该库不自动接管 `std::cout`、`printf` 等标准输入输出接口。

行输入示例为 `examples/console_input.cpp`，同样通过 Console 包提供依赖。`read_line("> ")` 等待一行，内部继续处理事件和刷新；回车提交。TW 使用 Backspace 删除，反斜杠作为普通字符；采集字母时按住 Shift 为大写，否则为小写（不处理 Caps Lock）。原版使用反斜杠 `\` 删除，字母仍为大写。需要配合现有主循环时，使用 `begin_input`、`flush`、`try_read_line`；详见 Console 文档。首版不支持 IME 文本输入及左右移动光标，标准流适配留到后续。

`scratch.hpp` 声明 `bool scratch::is_turbowarp()`，由目标 SDK 实现。它使用 TW 的兼容 `is turbowarp?` 参数报告积木，在 TW 编译及解释模式返回真，在原版返回假；不依赖扩展，也不以是否开启编译来判断平台。

通用 C++ 库也可以在自己的清单中声明：

```toml
[library]
sources = ["library.cpp", "detail/helpers.cpp"]
include_dirs = ["include"]
```

两组路径均相对于声明它们的清单。`sources` 接受现存的 `.cpp`、`.cc`、`.cxx` 文件；`include_dirs` 接受现存目录，并对项目及依赖源文件统一生效。重复引用同一源文件只编译一次，不同目录中的同名源文件使用独立输出路径。依赖库也遵循当前 Release/Debug 编译模式，编译数据库记录实际参数；首版不提供清单中的预编译 bitcode 字段。

## 运行和调试

在 VS Code 的扩展菜单中选择 **从 VSIX 安装**，安装编译器仓库提供的 `debugger/dist/scratch-llvm-debugger-0.1.8.vsix`。扩展 ID 为 `scratch-llvm.debugger`，不要求安装 TurboWarp Debugger 插件。也可执行：

```sh
code --install-extension /path/to/scratch-llvm/debugger/dist/scratch-llvm-debugger-0.1.8.vsix
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

VS Code 的 F5 / Ctrl+F5 使用随扩展附带的内嵌舞台，不打开在线 editor，也无需允许加载外部扩展。`turbowarp` 和 `turbowarpUrl` 不控制此内嵌模式。命令行 `scrate run --turbowarp /path/to/TurboWarp` 才可选择桌面应用。

也可以只构建调试产物：

```sh
scrate build --debug
```

Debug 产物为 `build/debug/project.sb3` 和 `build/debug/project.debug.json`。用户 C++ 代码使用 `-O0 -g -fstandalone-debug`，保留源码位置；LLVM 链接阶段仍启用 `--whole-program` 裁剪，但不执行完整 O2。预编译标准库保持 SDK 的优化设置。调试运行关闭 TW 编译；这些设置不改变普通构建的 O2 流水线。

调试映射必须与 SB3 配套，不要单独替换其中一个文件。调试构建会同时更新 `build/debug/compile_commands.json` 和供 IntelliSense 使用的 `build/compile_commands.json`；常规构建后 IntelliSense 切回常规编译参数。两种模式的 bitcode 和 SB3 分目录保存，互不覆盖。

## 编译范围

模板默认启用 Clang `-O2`、链接后 `default<O2>` 和 `--whole-program` 裁剪。编译器根据实际需要自动保留 `main`、后续浮点降级所需的辅助入口及 LLVM 特殊根，再删除未使用的库代码；不要另加只保留 main 的 `internalize` pass，否则可能提前删除浮点辅助函数。

普通 C++ `float`/`double` 运算可以直接使用。需要精确浮点运行库时，编译器自动从自身安装布局找到 `scratch-float.bc` 和 `SoftFloat-LICENSE.txt`，并把许可证放入 `.sb3`。可用 `SCRATCH_RUNTIME_DIR` 覆盖该目录；这与 `SCRATCH_STDLIB_DIR` 指向的 C++ SDK 是两个不同位置。

`examples/floating.cpp` 是小型浮点绘图示例，复制到 `src/main.cpp` 后按常规方式构建。当前 `scratch.hpp` 的画笔桥接参数仍为整数，需要时显式转换；浮点计算本身保留正常类型与精度语义。这里支持的是已实现的浮点运算和 intrinsic，不等同于已经提供完整 `<cmath>` 的普通函数运行库。

流程为 `C++ → Clang LLVM bitcode → scratch-llvm → .sb3`，固定使用 `x86_64-unknown-linux-gnu` 的小端布局，保证不同宿主生成一致的目标 ABI；**运行产物不需要 Linux**。模板只生成 bitcode，不调用宿主机器链接器。

默认 C++17、`-O2`，禁用异常、RTTI、线程及栈保护；局部静态对象使用单执行上下文的初始化保护。目标 SDK 基于 libc++ 22.1.8，提供第一批容器、算法、基础字符串、内存分配和对象生命周期支持。错误路径按禁用异常的配置终止程序，不能捕获异常。

分配器默认有 16 KiB 可复用堆，与其他静态数据、栈一起位于虚拟字节内存中；不是宿主堆。已支持固定 C locale 的窄字符 `iostream` / `sstream`；`std::cin/cout/cerr/clog` 通过 Console 接入画笔控制台，需要 SDK ABI 2。`printf` 家族支持基本窄字符格式；`scanf`、完整数学库、地区 locale、文件系统、线程及 RTTI 尚未提供。

SDK 的头文件和实现必须一起使用；不要改成宿主头文件或链接本机 `.lib/.so/.dll`。构建数据库会记录实际目标头文件路径，首次构建后代码补全使用相同配置。生成的 `.sb3` 包含 libc++ 许可证。

调试使用 LLVM 源码信息、Scratch 执行点映射、TW 解释器桥接及真正的 LLDB；容器美化查看暂未实现。LLDB 面对的是 Scratch 虚拟执行目标，程序并不会作为宿主机器码执行。

VS Code 配置格式参考：[Tasks](https://code.visualstudio.com/docs/debugtest/tasks)、[C/C++ 配置](https://code.visualstudio.com/docs/cpp/customize-cpp-settings)。

## 统一 scrate 入口

模板不包含 `scrate.py` 或 `build.py`。构建、运行和调试均使用已安装的命令：

```sh
scrate build
scrate run
scrate debug
```

尚未安装时，在编译器仓库根目录运行 `python -m pip install .`，或安装提供的 wheel。
确保 scrate 所在目录已加入 PATH；工具更新后重新安装即可，无需更新每个项目的启动脚本。
VS Code tasks 直接调用 `scrate build`；扩展 0.1.5 也直接调用已安装的 scrate。
如果调试器无法通过 PATH 找到命令，可在 launch.json 设置 `"scrate": "/absolute/path/to/scrate"`；
构建任务则相应修改其 `command`。运行中的 VS Code 若仍使用旧环境，可重新打开窗口。

`run/debug` 默认先构建；已有产物可加 `--no-build`。CLI 调试支持断点、单步、调用栈和变量查看。
可选 `[build]` 配置和完整参数见编译器仓库 `docs/SCRATE.md`。

## 保留 TurboWarp 设置

构建会在舞台写入 TurboWarp 原生配置注释（` // _twconfig_`），打开 SB3 时由
TurboWarp 自动读取；原版 Scratch 保留此普通注释，不启用这些额外功能。
即使没有配置表、直接调用 scratch-llvm，也默认使用下面的配置：

```toml
[turbowarp]
framerate = 60
high_quality_pen = true
offscreen_sprites = true
interpolation = false
unlimited_clones = false
remove_limits = false
stage_width = 480
stage_height = 360
```

`framerate` 是数值，可用小数，范围 0～250；0 表示跟随显示器刷新率。
其余开关必须为布尔值；舞台宽高必须为 1～8192 的整数。只需填写要覆盖的项。
仅根项目的 `[turbowarp]` 生效，依赖包不能修改全局设置。非法键和值会使构建失败。

TurboWarp 自己的保留格式不包含“循环计时器”和“禁用编译器”，所以不提供这两个
无效的持久化选项；新会话使用 TW 默认值（循环计时器关闭、编译器启用）。
F5 调试继续由调试桥接临时禁用 TW 编译器。设置的加载和重置行为沿用 TW 原生逻辑，
不强制覆盖浏览器已有的所有会话选项。

底层命令可使用 `scratch-llvm ... --turbowarp-settings settings.json`，JSON 对象
采用与该 TOML 表相同的字段。配置在生成项目及调试校验值之前写入，不修改已生成的 SB3。

格式依据：TurboWarp VM 的
[parseProjectOptions / storeProjectOptions](https://github.com/TurboWarp/scratch-vm/blob/develop/src/engine/runtime.js)。

## VS Code 内嵌播放器

扩展 0.1.5 起，F5 和 Ctrl+F5 在 VS Code 的 sCr++ 舞台面板中执行，不进入在线编辑器。
点击舞台后输入；关闭面板结束会话，暂时切换到代码标签不会销毁面板。
0.1.6 起舞台没有额外控制按钮，控制统一交给 VS Code 工具栏。普通运行结束后保留舞台及最后画面，手动停止会话或关闭面板后才清理。两种模式均关闭循环计时器。
命令行入口保持原有行为。
