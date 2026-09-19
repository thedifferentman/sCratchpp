# scrate 0.2

scrate 统一负责包管理、构建、运行和源码调试。默认仓库是
`https://scrate.shapy.cn`，采用静态文件索引。客户端只下载文件；`pack` 只生成本地文件，没有上传命令。

需要 Python 3.11+，运行时不依赖第三方 Python 包。可以直接使用：

```sh
python tools/scrate.py --help
```

也可以在本仓库执行 `python -m pip install .`，之后使用 `scrate` 命令。
CMake 安装同样提供 `scrate`；Windows 构建目录提供 `scrate.cmd`。

## 构建、运行与调试

在项目目录执行：

```sh
scrate build                         # Release 构建
scrate build --debug                 # Debug 构建与源码映射
scrate run                           # 构建后在 TurboWarp 中运行
scrate debug                         # Debug 构建后启动真实 LLDB 终端调试
scrate build --offline --locked
```

模板直接使用已安装的 `scrate`，不再附带 `scrate.py` 或 `build.py`。请先安装命令并加入 PATH。
工具开发时仍可从仓库根目录调用 `python tools/scrate.py build --manifest template/sCrpp.toml`。

`run` 和 `debug` 默认先构建，可用 `--no-build` 使用现有产物。
`run --debug` 运行 Debug 产物，仍不启动 LLDB；需要调试时使用 `debug` 子命令。
`--no-open` 输出本地连接地址，供手动打开浏览器；普通运行可用 `--turbowarp` 指定桌面版。
工具路径放在 `toolchain.local.json` 的 `clang`、`scratch_llvm`、`node`、`turbowarp`、
`debugger`、`lldb_dap`、`lldb_python` 中。`debugger` 可指向调试器目录或 `scratch-debug.cjs`。
运行需要 Node 和现有调试器运行文件；调试还需要可用的 LLDB。启动不会自动下载安装它们。

终端调试支持 `break file:line`、`delete file:line`、`clear`、`continue`、`pause`、
`next`、`step`、`finish`、`bt`、`locals`、`print expression`、`quit`。
表达式仅用于只读查看，不支持调用被调试程序函数或执行宿主 LLDB 命令。
连接过程中也可以输入 `quit`，退出、EOF 和 Ctrl+C 会清理 LLDB 及本地桥接。
`--command` 可重复传入命令，用于自动化调试；执行完这些命令后退出会话。

VS Code 的 F5 仍使用 Scratch LLVM Debugger 扩展；模板预任务调用 scrate 构建，
扩展 0.1.4 直接调用已安装的 scrate，通过 `scrate debug --dap --no-build` 启动协议后端。
`run --dap --no-build` 对应不启用 LLDB 的运行模式。DAP 的 stdout 只承载协议帧。

项目可选构建配置：

```toml
[build]
source_dir = "src"
include_dirs = ["include"]
output_dir = "build"
cpp_standard = "c++17"
```

默认构建 `src/**/*.cpp`。Debug 产物放在输出目录的 `debug/` 下。
支持 C++17/20/23，输出目录必须位于项目内，不能通过链接逃逸；不执行包或项目的自定义构建脚本。

## 项目依赖

```toml
version = 1

[package]
name = "my-app"
version = "0.1.0"

[dependencies]
console = "0.3.0"
```

```sh
scrate install
scrate build
```

模板构建会自动解析包依赖，接入头文件、源码或 bitcode，以及图片、列表、事件等资源。
Console 通过自己的清单依赖 Events 和 PTE，应用只需声明 Console。
基础 SDK 继续自动提供 C++ 标准库和底层运行时；不再自动附带这三个包。

第一版只接受精确的 `x.y.z` 版本，不解析 `^`、`~`、区间或 `latest`。
一个依赖图内，同一个包只允许一个版本和来源；冲突及依赖循环会报告错误。

本地开发使用路径依赖：

```toml
[dependencies]
console = { path = "../include/console", version = "0.3.0" }
```

路径相对于声明它的清单。仓库内的模板采用该方式，因此不需要实际网站就能构建。
`version` 对本地依赖可省略；准备打包发布时必须填写，并与本地包的版本一致。

## 锁文件与离线构建

`scrate install` 生成 `scrate.lock`，记录解析后的依赖图、来源、确切版本和 SHA-256。
请提交锁文件；包缓存无需提交。本地包也记录内容哈希，修改源码或资源后需要更新锁文件。

```sh
scrate install --offline --locked
scrate build --offline --locked
scrate list
```

`--offline` 禁止网络下载，使用已验证缓存或本地包；缺少所需内容时明确失败。
`--locked` 要求锁文件已存在且无需更改，不会自动接受依赖或本地包内容变化。
已有锁文件固定下载地址与内容哈希，避免同一版本在后续构建中悄悄换包。

可用 `--cache-dir` 指定 scrate 缓存，或设置 `SCRATE_CACHE_DIR`。
模板的 `toolchain.local.json` 也接受 `package_cache`、`registry` 和 `scrate`（客户端脚本路径）。

## 静态仓库

默认地址无需配置。测试或自托管时可指定：

```sh
scrate install --registry http://127.0.0.1:8080
scrate install --registry /path/to/local-registry --offline
```

项目也可配置 `[registry] url = "https://scrate.shapy.cn"`，环境变量为 `SCRATE_REGISTRY`。

静态目录协议：

```text
index/console/0.1.0.json
index/events/0.1.0.json
index/pte/0.1.0.json
packages/console/0.1.0/console-0.1.0.zip
packages/events/0.1.0/events-0.1.0.zip
packages/pte/0.1.0/pte-0.1.0.zip
```

每个索引 JSON 包含 `schemaVersion: 1`、`name`、`version`、`url`、`sha256`。
`url` 相对于仓库根目录，SHA-256 校验整个 ZIP。ZIP 根目录必须有 `sCrpp.toml`。
无需动态服务、账号系统或安装脚本即可提供下载。

```sh
scrate pack --manifest include/events/sCrpp.toml --output-dir build/scrate-registry
scrate pack --manifest include/pte/sCrpp.toml --output-dir build/scrate-registry
scrate pack --manifest include/console/sCrpp.toml --output-dir build/scrate-registry
```

这些命令生成可供网站托管的目录，**不会上传**。打包具有确定性，拒绝把不同内容写入已经生成的相同包版本。
`pack` 会把带版本的本地依赖转换为精确版本依赖。注册表包不能依赖本机路径，文件也不能逃出包目录。
以后发布新内容应增加版本号；不覆盖旧版本。

## 包清单与预编译代码

```toml
version = 1
[package]
name = "example-lib"
version = "0.1.0"
resource_namespace = "example-lib"

[library]
sources = ["src/library.cpp"]
include_dirs = ["include"]
```

包名与资源命名空间独立。现有包使用 `console/events/pte` 作为包名，保留
`__scl_console/__scl_events/__scl_pte` 作为资源命名空间，所以原生汇编中的列表名称不变。

预编译包使用以下字段，模板优先链接 bitcode，不再编译同一个包的 `sources`：

```toml
[library]
bitcode = ["lib/library.bc"]
include_dirs = ["include"]

[toolchain]
llvm_major = 22
target = "x86_64-unknown-linux-gnu"
scrpp_abi = "1"
```

三项 ABI 必须与消费方 SDK 一致。模板自动提供 SDK 信息；独立安装预编译包时使用
`--llvm-major 22 --target x86_64-unknown-linux-gnu --scrpp-abi 1`。
第一版不在 scrate 中运行编译或安装脚本，源码编译仍由项目构建流程负责。

## 验证

`tests/test_scrate.py` 验证解析、锁文件、缓存、版本冲突、ABI 和归档处理；
`tests/scrate_pipeline_test.py` 使用本地 HTTP 仓库打包三个真实库，验证 Release/Debug、
断网后的锁定构建、无依赖项目与本地路径依赖，并在原版 Scratch/TW 中执行。
测试不会访问 `scrate.shapy.cn`。
