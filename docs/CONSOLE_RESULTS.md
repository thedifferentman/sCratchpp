# 控制台、事件和 PTE 验证记录

2026-09-17，三个库分别位于 `include/console`、`include/events`、`include/pte`。
控制台通过清单依赖另外两个库；默认模板不自动链接字库，用户的 `template/src/main.cpp` 未被示例替换。

## 可重复测试

配置测试依赖后，构建并运行：

```sh
cmake --build --preset clang
ctest --preset clang -R 'console_model-library|events-library|pte-library|resources|resource-pack|template-build'
```

三个 Python 驱动也可以单独运行；它们接受 `--compiler`、`--clang`、`--sdk`、`--node` 和 `--output-dir`：

| 驱动 | 检查内容 |
| --- | --- |
| `tests/console_model_test.py` | 两个 VM 各 19 项断言；UTF-8 跨写入、宽字符、历史、增量绘制与覆盖重绘。字体使用计数桩，事件库为真实实现。 |
| `tests/events_test.py` | 真实鼠标滚轮和键盘 IO、队列容量、回调变更及重入、帽子不改 LLVM 内存或栈、再次绿旗重置、浮点时钟。 |
| `tests/pte_test.py` | 正式资源链接，42,221 字形和 65,536 项索引；两个 VM 各比对 212 条画笔/运动操作，含 12 次绘字和两次空尺寸调用。 |

Windows 和 Linux 均通过以上测试，使用原版 Scratch VM 解释执行和 TurboWarp VM 编译执行。Windows 另通过 17 项相关 CTest（含资源、字符串、汇编和调试器回归），以及 59/59 个 IR 回归用例。Linux 另通过 5 项基础 CTest。头部缺失字形按方框回退，不把 BMP 字库覆盖范围描述为完整 Unicode 字体。

## 模板与浏览器

复制模板到 `build/validation/console-template`，在复制品中链接控制台并采用 `template/examples/console.cpp`，生成：

| 模式 | SB3 | 积木数量 | 文件字节数 |
| --- | --- | ---: | ---: |
| Release | `build/validation/console-template/build/project.sb3` | 24,610 | 11,876,754 |
| Debug | `build/validation/console-template/build/debug/project.sb3` | 18,020 | 10,552,363 |

两者均只有舞台和唯一的 Program 执行角色；附加两个方向键采集帽子。文件大小包含字库的原生列表数据。Release 优化可能增加内联，因此此例的积木数量大于 Debug，不能把该对比视为性能基准。

在 TurboWarp 网页编译模式中运行最终 Release：

- 英文和中文实际画面正常；60 行示例可以滚回顶部。
- 对舞台派发浏览器 `WheelEvent` 能通过 GUI 输入处理触发滚屏。
- 直接 VM 输入测试中，按住上箭头不会滚屏；滚轮上移改变画面，下移恢复相同舞台截图。
- 空闲 `flush()` 持续让出执行，刷新时间戳推进，事件队列被消费。

浏览器低层 mouse-wheel 自动化曾把事件坐标置为 `(0,0)`，未命中舞台；检查事件目标后改用舞台 DOM 事件完成 GUI 链路验证。没有将这一自动化问题当作程序行为。

本次报告与截图保存在忽略的 `build/validation/`：

- `console-model/report.json`、`events-driver/report.json`、`pte-regression/report.json`
- `console-regression/report.json`
- `linux/console-events-pte-validation.json`
- `console-template/report.json`
- `console-ui/report.json`、`console-ui/stage-wheel-report.json`、`console-ui/final-top.png`

当前不包含标准输入、`printf`/`std::cout` 自动接入或一般键盘订阅；滚轮识别仍具有事件库文档所述的差分检测边界。应用需要保留主循环并主动 `flush()` 或 `poll()`；没有隐式后台 LLVM 线程。
