# 槽位运行时验证记录

日期：2026-09-13。对应 [共享槽位运行时](SLOT_RUNTIME.md)。这些结果验证当前实现，不表示所有程序都能获得相同比例的体积或性能改善。

## 构建与功能验证

Windows 编译器 SHA256：`39ca8c38fa3d00405e0867c9dca5ca4f677e0abf06e4a5d41b1e59978ca53506`。

| 验证 | 结果 |
|---|---|
| 原有及新增 IR 集成回归 | 58/58；正例在原版 Scratch/TurboWarp 两个 VM 执行，负例正确拒绝 |
| 直接槽位运行时 | 每个 VM 497 个案例、5,381 项逐字节检查，以及 4 类越界/指针错误检查通过 |
| 原有数值 AST 回归 | 34,736 项通过 |
| 编译期常量求值 | 11,292 项通过 |
| 非 VM CTest | 11/11 通过 |
| C++ 标准库组合与生命周期 | 22/22，包括大型 sequences_stress；两个 VM 共 44/44 次执行通过 |

原始报告：

- `build/validation/slot-complete-ir/report.json`
- `build/clang/test-output/slot-runtime/slot-runtime-vm-report.json`
- `build/validation/slot-stdlib/report.json`

独立 Linux Clang 构建通过；原有槽位/常量原生检查与六个针对性 IR 用例的双 VM 执行通过。最后增加的常量移位实现再次完成 Linux 构建、497 个槽位生成案例及常量原生检查，并通过常量移位、初始数据、浮点三个用例的双 VM 执行。

最终 Linux 编译器 SHA256：`de3d6305af3c0b0e0d78b3ae72c5fb56759de9ac8afad3eea48da2cdeb8c3729`。

Linux 报告：`build/validation/linux/slot-runtime-e2e.json`、`slot-runtime-constant-shift-native.json`、`slot-runtime-final-e2e.json`。macOS 本轮没有实机验证。

## 当前模板对比

源码仍为 vector 生成 40 个长度、std::sort、画笔绘图。前后均采用 O2 与全程序裁剪。基线来自先前保存的 `template/build/ir-analysis/pipeline-pruned.sb3`；新项目是 `template/build/project.sb3`。两者在同一 Windows 主机、相同 VM runner 下各运行一次，没有渲染器，因此这里没有验证像素，也不包含画笔渲染成本。

| 指标 | 改造前 | 槽位运行时 |
|---|---:|---:|
| 积木数 | 131,100 | 20,521 |
| SB3 字节数 | 27,889,985 | 5,240,755 |
| 原版 VM 执行 | 18.693 秒 | 1.303 秒 |
| TurboWarp 首次脚本编译 | 1.099 秒 | 0.227 秒 |
| TurboWarp 执行（扣除上述编译计时） | 50.7 毫秒 | 21.5 毫秒 |
| TurboWarp 进程峰值 RSS | 约 936 MB | 约 261 MB |

积木数量减少 **84.35%**。这是一组单次对照测量，不是跨设备统计基准；后续改动应保留同样的输入、优化选项和 VM 设置再比较。

基线项目 SHA256：`950afec77f0d705dd161a0ce7591f389b26b76982a9981ddeb14c71485184f22`。

新项目 SHA256：`4da9f9366120c5364375f795881ace6dabe7c1a256fed3c84e4630e1682b7074`。

数据：`build/validation/template-slot-size.json`、`build/validation/slot-template-benchmark.json`。

## 先前的大型标准库用例

这些用例使用标准库测试驱动的固定输入和选项，不能与上面的模板数字混为同一组基准。运行没有提高 VM 的默认 JavaScript 堆上限。

| 用例 | 旧项目 | 当前项目 | 当前原版执行 | 当前 TW 编译加执行 |
|---|---:|---:|---:|---:|
| map/set/unordered_map/string/unordered_set 组合 | 103.43 MB | 14.81 MB | 13.27 秒 | 1.398 秒 |
| vector/deque/list/string 组合 | 92.67 MB | 11.29 MB | 6.70 秒 | 0.877 秒 |

关联容器组合先前的原版 VM 运行在 180 秒时被测试限时终止；目前完成并通过结果检查。大型 sequences_stress 在先前暂停时没有完整执行结果，本轮首次得到两个 VM 的通过结果。对应的 TW 实际执行（扣除首次编译）约为 53.6 毫秒与 39.0 毫秒。

此前未完成、超时或失败的报告仍保留，不能作为通过结果引用；本页以上述当前报告为准。
