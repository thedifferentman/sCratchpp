# 键盘事件和行输入验收

本页前半记录初版验收；当前平台分支行为与补充验证见文末。最新 API 约定以 `include/events/README.md` 和 `include/console/README.md` 为准。

2026-09-17。在原有滚轮事件和画笔控制台上增加键盘采集与行输入；不接入 iostream。

## 接口及兼容范围

- `events::on_key` 和 `events::key::*`，与滚轮共用最多 16 个订阅。
- `[[events]] type="keyboard"` 生成 86 个键帽子，另保留两个滚轮帽子。只入原生列表，不调用 LLVM 回调。
- 可打印 ASCII 字符统一为大写字母键值；另有空格、Enter、四方向及 TW 的 12 个额外键。原版不触发 TW 额外键。
- 不合成 Tab、Alt、F1 等 VM 不可识别的按键，不提供松开、IME、粘贴或大小写还原。
- `console::begin_input/try_read_line/read_line/cancel_input/input_active` 提供一条行输入。反斜杠保留为事件码 92，仅在控制台活动输入时解释为删除；TW Backspace 也可删除。
- 阻塞等待内部继续刷新和让出执行；事件回调中禁止开始输入，避免分派重入。详细容量和取消语义见库 README。

## 自动化验证

Windows 和 Linux 均通过：

| 项目 | 内容 |
| --- | --- |
| 编译器资源与积木测试 | 键盘清单、86 个不同键帽子、引号/反斜杠字面值、大写去重、滚轮与键盘共存 |
| `tests/events_test.py` | 两个 VM 的 95 个可打印字符输入、方向/Enter、全部 TW 额外键、原版忽略、重复按键、普通键快速释放、128 条容量、溢出、LLVM 内存/栈不变和绿旗重置 |
| `tests/console_input_test.py` | 两个 VM 各两轮绿旗，每轮 22 项断言；真实键帽子驱动大小写、反斜杠删除、Enter 同批隔离、跨软换行、提示保护、容量、取消、阻塞读取、回调重入拒绝与 TW Backspace |

Windows 原有控制台模型测试也通过。持续集成入口：

```sh
ctest --preset clang -R 'blocks|resources|resource-pack|console_model-library|console_input-library|events-library'
```

本轮报告位于忽略的构建目录：

- `build/validation/events-keyboard-final/report.json`
- `build/validation/console-input/report.json`
- `build/validation/linux/keyboard-events-validation.json`
- `build/validation/linux/console-input-validation.json`

## 浏览器与模板

复制模板后使用 `template/examples/console_input.cpp`，链接正式 Console/PTE/events 库和完整字库。原 `template/src/main.cpp` 没有替换。

Release 产物为 `build/validation/console-input-template/build/project.sb3`。
在 TurboWarp 网页编译模式中通过浏览器按键验证：

- 输入 `XY`、反斜杠、`Z`、Enter，C++ 返回并打印 `XZ`。
- 输入 `AB`、Backspace、`C`、Enter，C++ 返回并打印 `AC`。
- 多次提交后会出现新的提示；输入 `QUIT` 后程序正常返回，`exit_code=0`。
- 实际画面包含中文提示、键入回显和返回字符串。

浏览器自动化的 `press Backslash` 名称未生成字面反斜杠；验证使用 `press '\'` 的实际字符完成。报告及截图位于 `build/validation/console-input-ui/`。

## TW 环境分支补充验证

新增 `scratch::is_turbowarp()`，使用兼容的布尔参数报告器。TW 通过采集时的 Shift 状态选择字母大小写：按住为大写，否则为小写，不处理 Caps Lock。控制台 TW 分支只把 Backspace 当作删除键，反斜杠可正常输入；原版维持反斜杠删除和大写字母。

本次 Windows 验证覆盖原版解释、TW 编译、TW 解释三个模式，各两轮绿旗、每轮 22 项行输入断言；实际验证环境判断返回值、Shift 按下/释放、反斜杠字面输入与两种退格策略。键盘事件单独验证了 Shift 快速松开的采样边界和同批多个不同按键。没有使用“最近按键”，没有新增查找造型、背景或扩展。

报告：`build/validation/console-input-platform/report.json`、`build/validation/events-shift-case/report.json`。交互示例已重建到 `build/validation/console-input-template/build/project.sb3`。
