# C1 快速画笔三角形

从用户提供的「C1 画笔快速画三角形.sb3」移植，沿用原算法：边长、内心、海伦面积，
8 层向内心收缩描边。全部几何运算在 Scratch 原生数值域中完成，不依赖 SoftFloat。

```cpp
#include <triangle/triangle.hpp>
int main() {
    scratch::triangle::draw(-120, -90, 0, 110, 120, -90, 0x4c97ff);
}
```

用 scrate 清单链接本地包，例如仓库模板目录：

```toml
[dependencies]
triangle = { path = "../include/triangle", version = "0.1.1" }
```

包没有对 Console/Events/PTE 的依赖，不需要造型或初始化列表，且不会自行清屏、
隐藏角色、创建帽子或让出线程。调用方可以和 PTE、画笔命令混合使用；Console 的
整屏重绘会清除相同画笔层上的其他图形。

接口接收 i32 整数顶点和 24 位 RGB，与当前 PTE 的桥接方式一致；内部插值保留小数。
退出时保持抬笔，不恢复角色位置、笔粗和笔色。临时 Scratch 变量使用 __scl_triangle_ 前缀；
单调用内不允许重入。全部生成过程为 warp。相同点/共线等计算面积非正的情况跳过描边。

这保留了原程序的 8 层近似填充，受 Scratch 原生数值精度及画笔最小/最大粗细约束，
不声称任意超大或极细三角形都有像素精确边界。舞台外坐标也受运行器的边界设置影响。

源码来源、哈希及适配差异见 SOURCE.json。原项目未提供明确许可证，本包不另行声称所有权
或授予原作的再分发许可。tools/extract_triangle.py 可以从原 SB3 重新生成算法；它只读取数据。


0.1.1 新增 draw_fixed(ax, ay, bx, by, cx, cy, rgb, units_per_pixel)。坐标除以正整数 units_per_pixel 后绘制，传 2 可表达半像素；非正倍率不绘制。原 draw 接口等价于倍率 1。
