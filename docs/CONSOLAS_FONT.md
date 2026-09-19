# Consolas 与微软雅黑字库转换

当前 `include/pte/resources` 的优先级为 Consolas Regular → Microsoft YaHei Regular → 原 PTE 字库。

- 输入：用户提供的 `font.zip`（转换器声明 MIT，作者 obdopqo），本机 `consola.ttf`（Consolas Regular 7.01）及 `msyh.ttc` 的 face 0（Microsoft YaHei Regular 6.31）。从 TTC 提取单字体后进行转换，不使用 Microsoft YaHei UI。
- 配置：`size=64`、`quality=16`；ImageMagick 7.1.2-31 Q8、Jimp 0.16.x。
- 转换器保留原有光栅化、骨架提取、线段简化和 PTE 编码算法。按 TTF cmap 过滤不支持的码点，只生成所需页面，避免缺字方框覆盖已有中文字形。
- 生成 2,479 个可用 Consolas 字形和 29,705 个微软雅黑字形。合并时保留全部 Consolas 字形，以微软雅黑补充 28,916 个，最后保留原字库的 12,630 个，总计 44,025 个。
- 95 个可打印 ASCII 字符的推进宽度均为 28 个 PTE 单位。PTE 坐标按转换器测得的最大字形高度归一化为 64 个单位，不直接使用 TTF 的 em 度量。
- 重新生成全部 65,536 项索引。原始 TTF/TTC 未加入源码；字库、转换工具和来源字体的哈希及原字库来源保存在 `include/pte/resources/SOURCE.json`。Consolas 与微软雅黑的归一化高度分别为 78、83。

保留当前绘制代码中的 `size / 16` 笔粗，以及控制台原有列宽和行距。生成的是画笔笔画近似，不是直接在 Scratch 中加载 TTF。

验证完成：

- 全部字形及索引一致性检查；95 个 ASCII 字符等宽；逐条验证 Consolas 优先、微软雅黑补齐和最后回退，不覆盖任何 Consolas 原字形。
- Scratch 解释模式和 TW 编译模式各 198 条实际笔迹操作与参考结果一致。
- 模板完整编译，TW 浏览器画面验证英文、符号和中文回退正常。

本次本地产物：

- 转换工作区及原数据备份：`build/font-conversion/consolas/`、`build/font-conversion/yahei/`。
- 转换器原始结果：`build/font-conversion/consolas/consola-s64-q16.txt`、`build/font-conversion/yahei/msyh-s64-q16.txt`。
- 合并校验：`build/font-conversion/yahei/merge-verification.json`。
- 测试报告：`build/validation/pte-yahei/report.json`。
- 舞台截图：`build/validation/yahei-ui/stage.png`。
- 可运行项目：`template/build/project.sb3`。

这些构建产物均被 Git 忽略；实际运行数据是 `include/pte/resources/font.txt` 和 `index.txt`。
