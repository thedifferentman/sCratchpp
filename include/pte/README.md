# PTE 画笔字库

这是独立的 scrate 绘字包，不依赖控制台或事件库。项目在 `[dependencies]` 声明
`pte = "0.1.0"` 后可包含头文件；模板自动链接包源码、字体和索引列表。
本地开发可改用 `{path = "../include/pte", version = "0.1.0"}`。
修改字库后重新构建以更新本地包锁文件，不需要重建基础 C++ SDK。

```cpp
#include <pte/pte.hpp>

scratch::pte::draw(0x4e2d, -10, 20, 24, 0x202020); // 中
int units = scratch::pte::measure_units(0x4e2d);
```

`draw(codepoint, x, y, size, rgb)` 使用左上角原点，坐标向右、向下展开；
字号 `size` 对应原字形的 64 个单位，当前笔粗为 `size / 16`。字宽接口返回
按归一化字形高度的 1/64 单位表示，因此自然推进量为 `units * size / 64`；
这里的高度由转换器测量，不是 TTF 的 em 度量。

固定列排版可以调用 `draw_cell(codepoint, x, y, height, cell_width, rgb)`。
它保留竖直字号和笔粗，只在自然字宽超出格宽时压缩横向坐标，并将字形
的自然推进区域居中。这样 `W` 和缺字方框不会沿用超出单元格的自然宽度。
非正字号或格宽不绘制。原 `draw` 接口的自然字形比例保持不变。

字形解析、坐标变换和画笔操作全部使用官方积木内联汇编，保留 Scratch
原生小数运算。一次绘字不主动让出执行，结束时抬笔。调用者管理角色的
可见性、位置和画笔状态，需要刷新时在字形之间让出执行。

当前按 **Consolas Regular → 微软雅黑 Regular → 原 PTE 字库** 的顺序选择字形。
配套转换工具使用 `size=64, quality=16`：保留 2,479 个 Consolas 字形，
以微软雅黑补充 28,916 个字形（包括中文），其余 12,630 个字形继续使用原字库。
合并后字体包含 44,025 个 BMP 字形，码点索引区分大小写。95 个可打印 ASCII
字符的推进宽度都是 28/64 个字号单位。控制台仍使用自己的固定格宽，不改变列数或行距。

不存在的字形（包括
未覆盖的非 BMP 字符）显示 `□`。数据放在 `__scl_pte::font` 和
`__scl_pte::index` 原生列表中，不占 LLVM 虚拟字节内存。

数据来源、转换参数及校验值见 `resources/SOURCE.json`。Consolas 和微软雅黑来自本机字体，
不随项目源码附带。微软雅黑使用 `msyh.ttc` 的普通 Microsoft YaHei 字体（face 0），不是 UI 变体。
转换时按字体 cmap 过滤字符，避免用缺字方框覆盖已有字形。韩文等两种字体均未覆盖的字符保留原字库。

下面的命令用于重新提取原始 PTE 字库，**会替换当前合并后的 Consolas 数据**：

```sh
python tools/extract_pte.py path/to/source.sb3
```

工具默认更新本目录的 `resources/`，保留字形记录中的空格并验证编码。
