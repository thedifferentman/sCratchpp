# 第三方来源与许可说明

sCr++ 自有代码采用根目录 [MIT License](LICENSE)。第三方代码、移植算法和字体数据
不因放入本仓库而自动改为 MIT；适用其原有许可及作者的授权说明。

## obdopqo 的 Scratch 项目

感谢 [obdopqo](https://gitblock.cn/Users/1123410) 提供以下项目。
下列内容于 2026-09-19 通过 Edge 中的项目公开页面核对，记录的是作者声明，
不是将声明改写为某一种标准开源许可证。

### B22 四万字中文画笔字库（PTE）

- 原作：[B22 四万字中文画笔字库](https://gitblock.cn/Projects/1135666)。
- 作者：obdopqo；页面显示发布时间 2022-11-01，版本 V5。
- 简介原文：“想要可以直接用，只要注明来源就行！”
- 作者在回复二次创作询问时也确认可以使用，并再次指向上述署名要求。
- 页面注明原字体使用思源黑体，并链接 [Adobe Source Han Sans](https://github.com/adobe-fonts/source-han-sans)。
- 本仓库将绘制逻辑移植为 Scratch 内联汇编，并适配字形索引、定宽单元格接口及
  字体回退；提取来源的文件哈希见 `include/pte/resources/SOURCE.json`。

本记录补充了先前仅检查离线 SB3 时未取得的作者声明。它不把 PTE 标记为 MIT，
也不表示作者能够授权后来替换的第三方字体。

### C1 画笔快速画三角形

- 原作：[C1 画笔快速画三角形](https://gitblock.cn/Projects/457546)。
- 作者：obdopqo；页面显示发布时间 2022-04-10，版本 V2。
- 简介明确写有：“欢迎大家引用！”
- 在评论区，“不同之者”询问能否使用该引擎制作其他引擎并注明来源，作者回复同意。
  这条回复对应当时的引用请求，不将其扩大解释为允许将原作重新许可为 MIT。
- 本仓库移植八层收缩描边算法，增加变量命名空间、固定点坐标接口、颜色/抬笔状态及
  退化三角形处理；原文件哈希和适配记录见 `include/triangle/SOURCE.json`。

## 字体数据的独立来源

当前 PTE 资源按 Consolas Regular、Microsoft YaHei Regular、原 PTE 字库的顺序组合：

- 2479 个 Consolas 字形；
- 28916 个微软雅黑字形；
- 12630 个原 PTE 回退字形。

原字体文件未打包，但转换后的字形数据位于 `include/pte/resources/font.txt`。
目前未补齐 Consolas/微软雅黑转换数据的分发许可依据；PTE 作者的引用声明及
字体转换工具的 MIT 声明不能替代这些字体的许可。

原 PTE 页面说明使用思源黑体；这补充了来源线索，但尚未逐项核对离线 SB3 字形数据的
字体版本和完整许可附件。记录来源不等于宣布整个混合字库采用 MIT。

## 其他随附组件

以下目录中的上游许可证和来源文件继续有效，应随相应交付物保留：

| 组件 | 仓库记录 |
| --- | --- |
| LLVM C API 头文件 | `third_party/llvm/LICENSE.txt` |
| libc++、LLVM libc | `third_party/libcxx/`、`third_party/llvm-libc/` 中的许可、来源和校验清单 |
| musl 代码 | `third_party/musl/COPYRIGHT`、`UPSTREAM.json` |
| nlohmann/json | `third_party/nlohmann/LICENSE.txt` |
| Berkeley SoftFloat | `runtime/softfloat/` 中的许可和来源文件 |
| TurboWarp Scaffolding 播放器 | `debugger/player/vendor/LICENSE`、`SOURCE.json` |

本文件随完整 CMake 安装提供。已发布的 scrate 包 ZIP 和网站文件本次保持原样，
没有用相同版本号覆盖它们；后续包发行也应附带适用的署名及许可材料。
