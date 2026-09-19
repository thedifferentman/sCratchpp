# 窄字符 iostream 首版

本实现保留 libc++ 22.1.8 的流、格式控制、streambuf 和状态位，在 LLVM 层链接，
通过 Console 0.2.0 的标准输入输出适配接入画笔控制台。不需要宿主文件系统。

## 使用

先重新构建并安装最新编译器及基础 SDK。Console 0.3.0 已发布到默认注册表，包含标准流适配及局部擦除；
本仓库的模板和 `examples/iostream/` 均可通过包名安装。
项目使用本地包时可填写实际相对路径，例如模板目录中：

```toml
[dependencies]
console = { path = "../include/console", version = "0.3.0" }
```

当前推荐直接使用 `console = "0.3.0"`。旧的 0.1.0 包不含适配器，旧版本文件继续保留。

```cpp
#include <iostream>
#include <string>
int main() {
    std::cout << "Name: ";
    std::string name;
    if (std::getline(std::cin, name)) std::cout << "Hello, " << name << "!\n";
}
```

无需显式调用 Console 初始化或刷新。`cin` 的绑定流在读取前刷新；`endl/flush`、
`cerr` 的 unitbuf、正常返回和 `exit()` 会刷新。单独输出换行不保证立即绘制。
`abort`、trap、`_Exit`、Scratch 停止按钮不保证退出刷新。

## 范围

- `cin/cout/cerr/clog`、整数/float/double、字符串和 `getline`。
- 常用 `hex/dec/oct`、`boolalpha`、`fixed/scientific/hexfloat/defaultfloat`、
  精度、宽度、填充、对齐、符号及 showbase/showpoint。
- `stringstream/istringstream/ostringstream`、内存流 seek、peek/get/unget、流状态。
- 固定 C locale 的字符分类、数值标点及 `locale::classic()`；C/POSIX 名称可用。
  不提供货币/时间/消息目录 facet 或系统地区数据库。自定义 facet 可按 libc++ 接口安装。
- 窄字符数值转换使用 LLVM libc 的精确解析；浮点输出使用 musl 1.2.5 精确转换核心。
  libc++ charconv 源码也随 SDK 提供。转换不使用 Scratch 宿主数值字符串格式。
- 最小 FILE 支持标准流读写、单字节回退、EOF/error、清除状态、刷新和缓冲模式。
  printf 家族支持窄字符串、字符、整数、指针及浮点；不支持 `%n`、位置参数或宽字符格式。
  scanf 家族及文件流仍未实现。不要把声明存在视为完整 libc 实现。

stdout/stderr 显示到同一 Console，但保留各自 FILE 和 C++ 流状态。实现单执行上下文；
禁止在事件回调中阻塞读取。未链接终端提供者时，最终编译会明确报错，提示链接 Console 0.2.0；不再静默进入失败状态。
仅使用 stringstream 不需要 Console 包或字体资源。

## Console 新能力

`InputStatus` 区分 idle、pending、line、cancelled、eof、busy。
`read_line_result(text, prompt, max_bytes)` 阻塞等待明确结果；
`try_read_line_result(text)` 非阻塞消费结果；空字符串的 line 是合法空行。
只有 line 会覆盖 text。原有 read_line/try_read_line 接口继续保留。

`close_input()` 放弃当前编辑并设置持久 EOF；`reopen_input()` 允许重新开始输入。
已经进入 FILE 缓冲区的字节仍会先被读完；重新打开后，使用者应执行 `std::clearerr(stdin)` 和 `std::cin.clear()`，清除两层 EOF/错误状态。
没有新增 EOF 键盘快捷键。`cancel_input()` 返回取消，回调重入或已有输入占用返回忙碌。
标准流适配把每个提交行追加 `\n` 后提供字节，支持一行内连续多次提取。
标准输入默认一行最多 1024 字节；沿用现有 ASCII 编辑、TW Shift/Backspace 支持。

## ABI 与容量

SDK ABI 升为 2：所有源码使用 `-mlong-double-64`，long double 为 64 位；不提供 x86
80 位扩展精度。float/double 的 IEEE 位级语义保持不变，异常、RTTI、线程仍禁用。
旧 ABI 预编译 bitcode 必须重建；源码依赖自动使用当前 SDK 重新编译。

SDK 建议虚拟内存为 200000 字节，scrate 自动采用；可在 `[build]` 用 `memory_bytes`
覆盖（1024～200000），但流和数值库需要足够静态区及栈空间。直接调用 scratch-llvm
构建 SDK 程序时使用 `--whole-program`，完整流程序再加 `--memory 200000`。堆仍默认 16384 字节。

普通库不包含 `<iostream>` 时不会额外初始化标准流；流运行时按最终可达性裁剪。
whole-program 流水线在 O2 前后均执行裁剪，以免优化完全无用的库函数。

## 验证入口

- `tests/iostream_test.py`：真实 libc++ 的格式化、提取、字符串流、边界值和退出顺序。
- `tests/console_input_test.py`：真实键盘及输入状态，覆盖原版和 TW 两种执行方式。
- `examples/iostream/`：真实画笔终端交互示例。
- `build/validation/iostream-prompt.png`、`iostream-completed.png`：浏览器实际画面。

功能测试不代表大型 iostream 程序已经轻量化。TW 首次编译生成的积木仍有开销；
原版解释器执行复杂数值格式测试明显较慢，测试记录会单独保留时限和结果。

## 2026-09-19 验证结果

- 完整 30 项流/数字检查在 TW 和原版 Scratch 通过，包括最大 double、最小次正规数、
  ties-to-even 舍入、整数范围错误、经典 locale、全局构造输出与析构后的刷新。
  当前记录：`build/validation/iostream-final/report.json`；TW 总执行约 5.8 秒（含首次编译），
  原版解释执行约 214 秒。原版综合测试默认时限提高至 600 秒，避免把已知解释开销误判为挂死。
- Console 输入状态：原版、TW 编译、TW 解释模式各连续两次绿旗通过。
- 基础内存、new 对齐、析构/exit/quick_exit 等 7 项回归在双 VM 通过。
- 新增 invariant 标记、cttz.elts 固定向量支持，以及既有浮点最终链接裁剪，在双 VM 通过。
- 已安装布局中的 SDK 与 scrate 能构建独立 iostream 示例的 Release 和 Debug。
- 实际浏览器完成姓名与浮点输入，并在 main 返回后显示未显式 flush 的最终一行。

这是功能验收，尚未覆盖完整 C++ 标准库一致性、全部 printf 格式或新版 Linux/macOS 实机运行。


Console 0.3.0 改为白底字符区域局部擦除，新增 Triangle 0.1.1 依赖。标准流接口不变；更新包后重建项目即可。
