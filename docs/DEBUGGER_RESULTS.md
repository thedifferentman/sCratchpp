# 首版源码调试验证

## VS Code 接入修复（0.1.1）

修正激活事件为 VS Code 支持的 `onDebug`。直接运行配置使用扩展声明的 `mode: "run"`，不将协议字段 `noDebug` 写入 launch.json；适配器在运行模式中自行设置该协议字段。

`debugger/tests/vscode_activation.cjs` 在真实 VS Code 扩展测试宿主中调用 `startDebugging`，未手动激活扩展：验证自动注册适配器、真实 LLDB 停在 C++ 第 10 行、Run 不启动 LLDB，以及 JSON 语言服务对模板 launch.json 无错误。报告位于 `build/validation/vscode-extension-report.json`。

实现及使用方式见 [debugger/README.md](../debugger/README.md)。当前使用真实 LLDB-DAP，通过 IR 远程目标连接 TW 解释器；不使用 TW Debugger 插件，不固定 TW 版本。

## 已验证

- LLVM 调试信息：指令位置、基本类型、参数/局部变量、不可用值、浮点展开的位置传递。
- 同一份带调试信息的 bitcode，开启或关闭 `--debug-map` 后的 `project.json` 完全相同。独立映射通过 CRC32 关联正确产物。
- 真实 LLDB → DAP → GDB remote → WebSocket → TW：源码断点、循环连续三次命中、递归三层调用栈、源码进入/跳过/跳出、读取 `sum = 3` 和递归参数 `n = 2`、正常退出码 0。
- 程序运行中增加断点并命中、运行中删除断点后继续运行、外部暂停和主动断开。一次对照运行中两次暂停为 140 ms、138 ms，增加断点至命中为 155 ms；测试上限为 2 秒，非实时性能保证。
- TW 解释器：时间片、异步 Promise 恢复、暂停计时、缓存重置、递归调用实例及解除调试钩子。
- 当前在线 TurboWarp 浏览器版：自动加载本地桥接及项目，关闭编译，停在真实 C++ 第 12 行；Blockly 工作区完整加载，浏览器错误列表为空。
- 原有 58 项 IR 回归全部通过：正例在 Scratch VM 与 TW 编译模式执行，负例正确拒绝。
- 常规与 Debug 模板均实际构建成功；Debug 使用 `-O0 -g -fstandalone-debug` 及裁剪。VSIX 已打包并在本机 VS Code 安装。
- Linux 已重新构建编译器和前端/积木测试，通过相关检查及真实 TW 调试引擎测试；未在 Linux 验证 LLDB。记录见 `build/validation/linux/debugger-validation.json`。

Windows LLDB 使用现有 LLVM 22.1.8，缺少的 Python 3.11 DLL 和标准库由官方嵌入包提供，私有放置于 `build/debugger/python311`，不依赖 CLion 或系统 Python 替换。

## 顺带修复

1. O0 libc++ 中残留的 `llvm.is.constant` 和 `llvm.objectsize` 通过 LLVM 自身的必要降级处理，保留 `optnone`，不启用完整 O2。
2. 数值形式的 LLVM 布尔条件连接到 Boolean 输入时生成显式零比较，避免 VM 可执行但 Blockly 编辑器拒绝加载。
3. 外部暂停通过 RSP 报告 SIGINT，使 LLDB 在运行中更新断点后能够自动继续。
4. 运行时错误不会因尚未设置退出码而被报告为成功退出。

## 产物

- `build/validation/debugger-regression/report.json`
- `build/validation/debugger/lldb-dap-report.json`
- `build/validation/debugger-running/lldb-dap-report.json`
- `build/validation/debugger/browser-report.json`
- `build/validation/debugger/browser-errors.json`
- `build/validation/debugger/browser-paused.png`
- `build/validation/debugger-rsp/report.json`
- `debugger/dist/scratch-llvm-debugger-0.1.1.vsix`

## 范围

首版使用 64 位小端指针，只调试 TW 解释执行。容器美化、完整表达式执行、修改变量、数据断点、反向调试及多执行线程延后。复杂或优化后的变量位置表达式可能不可用；模板预编译标准库不保证有源码调试信息。浏览器集成与完整 LLDB 链路在 Windows 验证；其他宿主的验证范围分别记录，不将可移植实现等同于所有平台都已实测。
