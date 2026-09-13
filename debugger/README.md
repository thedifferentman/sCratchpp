# Scratch LLVM 源码调试

调试使用 **真正的 LLDB（`lldb-dap`）**，程序仍在 TurboWarp 的解释器中执行。无需 Debugger 插件，不修改 SB3 积木，也不固定 TurboWarp 版本；启动时检查所需接口，不兼容时给出错误。

```text
VS Code ─ DAP ─ scratch-debug.cjs ─ DAP ─ lldb-dap
                                            │
                                      DWARF + GDB remote
                                            │
                                     LLVM IR 调试目标
                                            │
                                   本地 WebSocket 桥接
                                            │
                                 TurboWarp 解释器执行钩子
```

LLVM 调试信息描述源码和变量，编译器记录 IR 到积木的映射。宿主生成一个仅供 LLDB 读取的 ELF/DWARF 文件，为 IR 指令赋予虚拟执行地址。该文件的机器码不在宿主 CPU 上执行；真实执行、内存读取和暂停仍由 TW 完成。

## 安装与启动

1. 安装可用的 `lldb-dap`、Clang 和 `ld.lld`。可通过 `lldbDap`、`clang` 配置路径，也可使用 PATH。模板的 `toolchain.local.json` 中 `clang` 会被读取。
2. Windows 的某些 LLDB 发行版还需要 Python 3.11。根目录运行 `python tools/bootstrap_debugger.py --prepare-python` 可明确下载并准备私有运行时；调试启动本身不联网安装依赖。也可配置 `lldbPython`，或在 `toolchain.local.json` 中设置 `lldb_python`。不修改系统环境变量。
3. 根目录运行 `python debugger/pack_extension.py`。
4. 在 VS Code 的扩展菜单选择“从 VSIX 安装”，选择 `debugger/dist/scratch-llvm-debugger-0.1.1.vsix`。
5. 打开 `template/`，选择 **Scratch: Debug** 并按 F5。预启动任务会生成 `build/debug/project.sb3` 和匹配的 `project.debug.json`。
6. 浏览器打开 TurboWarp；首次运行时，根据浏览器/TW 提示允许加载本地扩展及访问 localhost。连接后会自动加载项目、关闭 TW 编译，并在入口停住。

默认桥接监听 `127.0.0.1:8000`。自动加载无沙箱扩展要求 URL 使用 `http://localhost:8000/`，因此首版同时只支持一个浏览器调试会话。如果端口已占用，结束占用它的服务或另一调试会话后重试。每次会话有随机 URL token，并检查浏览器 Origin；服务仅提供本次项目和调试资源。

直接运行请选择 **Scratch: Run in TurboWarp**。配置 `turbowarp` 为桌面版可执行文件时，直接传入 SB3；未配置时使用浏览器版，加载后正常运行。直接运行不需要 LLDB。

```json
{
  "type": "scratch-llvm",
  "request": "launch",
  "name": "Scratch: Debug",
  "project": "${workspaceFolder}/build/debug/project.sb3",
  "debugMap": "${workspaceFolder}/build/debug/project.debug.json",
  "preLaunchTask": "Scratch: Debug",
  "stopOnEntry": true
}
```

可选配置包括 `lldbDap`、`lldbPython`、`clang`、`node`、`turbowarpUrl` 和 `connectTimeout`。Node 默认复用 VS Code 自带运行时，不需 npm 安装依赖。

## 首版范围

- 源码断点可以运行时增加、删除；收到设置后下一次执行到该位置生效。
- 暂停、继续、源码进入/跳过/跳出，以及函数调用栈由 LLDB 与 IR 目标协作处理。
- 基本局部变量和参数由 DWARF 类型、位置表达式及实际内存读取显示。未提供充分调试信息的值可能不可用；不会凭旧槽位内容推测变量。
- 容器美化查看、完整 C++ 表达式求值、写变量、数据断点、多线程、反向调试不属于首版。尤其不支持调试表达式调用被调试程序中的函数。
- 首版符号适配使用 64 位小端指针的虚拟目标；不代表在宿主上原生运行 x86-64 程序。
- Debug 构建减少 LLVM 优化；关闭 TW 编译本身不会恢复被 LLVM 优化掉的源码信息。
- Warp 程序通过调试执行钩子定期让出时间片，保证页面和外部暂停请求可处理。调试执行会比 Release 慢。

`project.debug.json` 的 `projectCrc32` 必须与 SB3 的 `project.json` 匹配；更改项目后应重新运行 Debug 构建。符号辅助产物 `project.debug.elf`、`.s`、`.o`、`.json` 与项目放在一起，可以随构建目录删除。

## 实现与验证入口

- `scratch-debug.cjs`：公开启动入口、真实 LLDB DAP 转发和会话生命周期。
- `llvmdbg.cjs`：本地传输、浏览器引导及内部测试用协议工具；文件名不是额外的 LLDB 实现。
- `symbols.cjs`：从 LLVM 调试映射生成 DWARF。
- `ir-target.cjs`、`rsp.cjs`：IR 执行目标与 LLDB 的 GDB remote 协议。
- `engine.js`：TW 解释器能力检查、执行边界、暂停恢复与 IR 地址转换。
- `vscode/`：VS Code 扩展。

```text
node --test debugger/tests/transport.test.cjs
node debugger/tests/lldb_dap_e2e.cjs
node debugger/tests/lldb_dap_e2e.cjs --running
```

第二项需要先生成 `build/validation/debugger/program.sb3` 及调试映射，并提供可用 LLDB/Clang。它使用真实 LLDB、WebSocket 和 TW VM；浏览器许可与扩展自动加载需另行进行浏览器验证。

`--running` 独立编译一个长循环测试，验证程序实际运行期间增加断点并命中、删除断点后继续执行、外部暂停和主动断开。它不等待长循环跑完；暂停响应有 2 秒验收上限，单次实测约 0.14 秒，不构成所有宿主的实时性能保证。报告分别位于 `build/validation/debugger/lldb-dap-report.json` 和 `build/validation/debugger-running/lldb-dap-report.json`。

接口依据：[TurboWarp URL 参数](https://docs.turbowarp.org/url-parameters)、[无沙箱扩展](https://docs.turbowarp.org/development/extensions/unsandboxed)、[LLDB DAP](https://lldb.llvm.org/use/lldbdap.html)。
