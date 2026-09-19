# 从源码安装并运行第一个项目

本项目目前面向开发者 Alpha 测试。以下从已经克隆的仓库根目录开始；不需要提交或下载他人的 `build/`。

## 1. 准备宿主工具链

- Windows x64：安装 Python **3.14+**，执行下面的准备脚本。它下载并校验独立 Clang 22.1.8、libc++、LLD、LLVM、CMake 和 Ninja，不需要 Visual Studio。
- Linux：使用 [构建说明](BUILDING.md) 中的 Ubuntu 24.04 x86_64 准备流程，或自行配置匹配的 LLVM 22.1 工具链。
- macOS：提供了构建配置，但尚未完成实机验收，不能保证开箱即用。

Windows PowerShell：

```powershell
python tools/bootstrap_windows.py
. ./build/toolchains/windows/activate.ps1
```

激活只作用于当前终端。之后每个新终端都要重新激活，或自行配置工具链 PATH；VS Code 也必须继承这些路径。Windows 准备脚本要求 Python 3.14 是为了读取 Zstandard 压缩包，安装后的 scrate 本身要求 Python 3.11+。

## 2. 构建并安装

```sh
cmake --preset clang
cmake --build --preset clang --parallel 6
cmake --install build/clang --prefix build/install
```

安装前缀可以换成自己选择的目录。`build/install` 是便于本地试用的示例，不是模板的固定依赖；使用安装目录时，应保留整个 `bin/` 和 `share/` 布局。目标 SDK 和 SoftFloat 会从仓库随附源码生成。

将安装目录的 `bin` 加入当前终端 PATH：

```powershell
# Windows，仍在仓库根目录
$env:PATH = (Join-Path $PWD 'build/install/bin') + ';' + $env:PATH
scratch-llvm --version
scrate --version
clang++ --version
```

```sh
# Linux/macOS，仍在仓库根目录
export PATH="$PWD/build/install/bin:$PATH"
scratch-llvm --version
scrate --version
clang++ --version
```

CMake 安装已经提供 scrate 及配套 Python 模块，无需重复 pip 安装。也可用 `python -m pip install .` 单独安装 scrate，但它**不包含**编译器、目标 SDK、Clang、LLDB 或播放器。运行 `scrate` 的 Python 必须为 3.11+；Unix 安装入口使用 `python3`，Windows 入口使用 `python`。

## 3. 复制模板并构建

复制仓库的 `template/`，或安装目录的 `share/scratch-llvm/template/` 到自己的工程目录。不要复制模板已有的 `build/`、`.scrate/` 或 `toolchain.local.json`。

进入复制后的目录执行：

```sh
scrate build --locked
```

首次构建从 `https://scrate.shapy.cn` 下载锁定版本的包并校验 SHA-256；后续可使用 `scrate build --offline --locked`。默认 Console 0.3.0 依赖 Events 0.1.0、PTE 0.1.0、Triangle 0.1.1，不需要仓库外的本地包路径。

产物为 `build/project.sb3`。在 TurboWarp 或 Scratch 中上传并点击绿旗，当前模板读取两个整数并输出它们的和。舞台获得焦点后输入，例如 `17 25` 后回车，应显示 `42`。

若找不到工具，复制 `toolchain.example.json` 为 `toolchain.local.json` 并填写实际路径。若报 SDK 缺失，请检查完整安装布局，不要改为宿主标准库头文件。更多选项见 [模板说明](../template/README.md)。

## 4. 在 VS Code 中运行和调试

在仓库根目录生成扩展安装包：

```sh
python debugger/pack_extension.py
code --install-extension debugger/dist/scratch-llvm-debugger-0.1.8.vsix
```

也可通过 VS Code 的“从 VSIX 安装”菜单选择该文件。用 VS Code 打开复制后的模板目录；Microsoft C/C++ 扩展用于补全，Scratch LLVM Debugger 扩展用于运行和调试。

- `Ctrl+Shift+B`：构建 SB3。
- 选择 **Scratch: Run in TurboWarp** 后启动，或 `Ctrl+F5`：在内嵌舞台运行，不需要 LLDB。舞台在程序结束后保留。
- `F5` / **Scratch: Debug**：需要额外安装 `lldb-dap`（或兼容的 `lldb-vscode`）、Clang 和 `ld.lld`，使用 LLDB 调试源码。Windows 工具链准备脚本不替代 LLDB 安装。

VS Code 内嵌模式使用自带的 Node 运行时；命令行 `scrate run/debug` 需要额外的 Node.js 20+，并保留外部浏览器/桌面启动方式。Windows 某些 LLDB 发行版还需要配套 Python DLL，诊断及准备命令见 [调试器说明](../debugger/README.md)。

修改 PATH 后完全退出并重新启动 VS Code，以免扩展继承旧环境。默认 F5 在入口停住，继续执行后才会出现输入提示。

## 5. 验证和边界

维护者运行测试还需安装测试专用依赖：

```sh
npm --prefix tests ci --ignore-scripts
cmake --preset clang
cmake --build --preset clang --parallel 6
ctest --preset clang
```

当前配套版本为编译器 0.1.0、scrate 0.2.0、SDK ABI 2、扩展 0.1.8、Console 0.3.0。禁用异常、RTTI 和线程；未提供文件系统、完整数学库或容器调试美化。控制台行输入限 ASCII，显示可用随包字库。

最新发布检查、验证范围和尚需维护者确认的分发材料见 [RELEASE_READINESS.md](RELEASE_READINESS.md)。
