# scrate 0.2 构建、运行与调试验证

构建逻辑已迁入 `tools/scrate_build.py`，运行与调试分派位于 `tools/scrate_run.py`。
`scrate build/run/debug` 是统一入口，项目 `build.py` 只保留兼容转发。

本次验证：

- 包管理 20 项、构建 18 项、项目启动器 9 项、运行分派 8 项测试通过适用平台测试。
  Windows 的普通符号链接测试因权限跳过，真实 NTFS junction 测试已执行并通过；Linux 跳过 Windows junction 测试。
- Windows/Linux 均通过 8 个构建案例、16 次 VM 执行，包括无项目构建脚本的直接 scrate 构建、
  Release/Debug、源码包、真实 bitcode 包和锁定离线构建。
- Windows 真实 LLDB/TW 测试覆盖动态断点、调用栈、局部变量、只读表达式、进入/跳出/跳过、继续至退出，
  并验证连接期间 quit、EOF、超时的进程与端口清理。Linux 没有现成 LLDB，跳过两项真实 LLDB 测试。
- 通过 Python scrate 入口实际启动当前模板：`run` 在 TW 正常执行；`debug` 停在 `main`，执行 `bt` 后退出。
- 在独立虚拟环境安装 `scrate-0.2.0` wheel，只有源码、没有模板头文件/项目构建脚本的最小项目构建成功；
  `run --no-open` 超时正确返回非零状态并退出。
- VS Code 启动器测试通过，扩展已更新并安装为 0.1.3。模板预任务和 DAP 后端都通过 scrate 启动。

实际模板的 Debug 构建还暴露了 `llvm.ucmp` 缺口，本次补充 `ucmp/scmp` 的精确整数三路比较。
新增标量、宽整数和固定向量正例，以及非法返回位宽负例，Windows/Linux 双 VM 验证通过。

报告和产物（均位于忽略的构建目录）：

- `build/validation/scrate-cli-pipeline/report.json`
- `build/validation/debugger-cli/cli-report.json`、`cli-cleanup-report.json`
- `build/validation/scrate-launch/run-report.json`、`installed-report.json`
- `build/validation/scrate-debug-ui.log`
- `build/validation/linux/scrate02-validation.json`
- `build/validation/scrate-three-way-compare/report.json`
- `build/validation/linux/three-way-compare-validation.json`
- `build/validation/scrate-dist/scrate-0.2.0-py3-none-any.whl`

当前模板产物为 `template/build/project.sb3` 和 `template/build/debug/project.sb3`，
Debug 映射位于相邻的 `project.debug.json`。

## 2026-09-17：Windows VS Code 启动管道修复

F5 和 Ctrl+F5 经 scrate 启动时，Python 子进程默认的隐式标准句柄继承不能保证
Windows DAP 管道可用，表现为适配器静默退出、没有 initialize 响应。
`scrate_run` 现在显式传递 stdin/stdout/stderr，保持协议字节直接传输。

验证：

- `test_scrate_run.py` 9 项通过，新增真实子进程往返测试，覆盖 run/debug 的管道传递。
- 已安装的 scrate 经 VS Code 的 Code.exe Node 模式启动后，run 和真实 LLDB 22.1.8
  均返回成功的 initialize 响应。
- 独立 VS Code 扩展测试宿主验证 debug、run、noDebug 三种启动方式通过，debug
  到达停止点并读取调用栈；noDebug 使用 VS Code 的 Run Without Debugging 参数。
  此测试连接真实 TurboWarp VM，未模拟浏览器授权界面的用户操作。
- 报告：`build/validation/vscode-extension-report.json`。
