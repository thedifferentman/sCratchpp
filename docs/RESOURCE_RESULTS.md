# 资源与字符串运行时验证

实现接口见 [资源说明](RESOURCES.md)，用户示例为
[template/examples/resources.cpp](../template/examples/resources.cpp)。

## 完成范围

- `sCrpp.toml`、独立资源包和多包链接、包名与造型名称冲突检查。
- PNG 原字节嵌入 SVG；保留源分辨率，默认 1:1 逻辑尺寸，可指定尺寸与中心。
- SVG 使用外层逻辑画布，保留内层 viewBox、变换及宽高比规则。
- 同一最终 SVG 文件只打包一份，不合并具有不同名称/中心的造型条目。
- 全部造型挂在单个 `Program` 角色，每次绿旗恢复隐藏和默认空白造型。
- 自动生成 `scratch.cpp`；头文件只声明接口，普通与 Debug 构建均支持。
- `scratch::set_string` 写入 `__scl_string`；造型按完整名称直接交给 Scratch 切换。

## 自动测试

- 资源准备：16 项 Python 单测，包括路径、尺寸、非零 viewBox、PNG 数据保留、重复包及循环依赖。
- 资源内容标识：7 个 RFC 1321 MD5 标准测试向量。
- 模板构建：4 项测试，验证生成源文件、两种构建模式、包名前缀和用户源码保留。
- 真实 Scratch VM 与 TW 编译模式：每种运行两轮绿旗，验证名称切换、中文/emoji、数字样式名称、未知名称回退、显示隐藏和重置。
- UTF-8 桥接：每个 VM 验证 24 个精确字符串案例，覆盖大小写、中文、组合字符、emoji、嵌入 NUL、长文本及非法编码。
- 原有 58 项 IR 回归全部通过。
- Linux 已重建编译器与 SDK，通过资源/模板/汇编检查，以及上述资源流水线与 24 项字符串的双 VM 执行。未在 Linux 做渲染像素检查。

## 浏览器渲染

在在线 TurboWarp 的真实渲染器中加载生成的 SB3 并自动运行：

- PNG 造型显示尺寸为 64×48、中心为 (7,9)，嵌入原 PNG 为 640×480。
- SVG 图章为 20×10，中心为 (10,5)。
- 角色最终隐藏，画笔层保留两个图章；程序退出码 0。
- 480×360 舞台快照中检测到 3102 个非白像素，其中 200 个为 SVG 的精确填充色；边界为 (230,171)–(296,218)，与指定位置/中心一致。
- 独立验证源 SVG `viewBox="10 20 20 10"` 经包装后逻辑尺寸 80×40、中心 (40,20)。
- 浏览器错误列表为空。

验证中修复了自动启动时 SVG 尚未解码导致首次盖章为空白的问题：VS Code 运行/调试桥接先等待图片加载，再发出 ready 或启动程序。对应扩展版本为 0.1.2；该等待不写入 SB3 积木。

## 边界

原版 Scratch 导入器会删除 U+0008，因此 `set_string` 将其替换为 U+FFFD；非法 UTF-8 按字节替换为 U+FFFD。资源逻辑名不允许控制字符，不受这个边界影响。高清图片数据保留不等于原版画笔层变为高清；最终图章分辨率仍受宿主画笔设置影响。

报告与图像位于：

- `build/validation/resources-final/report.json`
- `build/validation/resources-final/stage.png`
- `build/validation/resources-final/pixels.json`
- `build/validation/resources-final/svg-geometry.json`
- `build/validation/resource-string/report.json`
- `build/validation/resources-regression/report.json`
- `build/validation/linux/resources-validation.json`
