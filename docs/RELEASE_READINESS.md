# 仓库上传与 Alpha 交付检查

日期：2026-09-19。范围：源码、Git 检出、Windows 安装布局及模板首次使用。
本次没有提交、推送或改动线上包文件。后续经维护者确认，主项目已采用 MIT，并通过 Edge 核对了引用项目的作者声明。

## 结论

技术上已具备面向开发者的源码 Alpha 使用路径：准备工具链 → CMake 构建并安装 →
复制模板 → scrate 下载包并构建 → 安装 VSIX 运行/调试。
不需要上传 build 目录。新用户步骤见 [QUICKSTART.md](QUICKSTART.md)。

主许可证和绘图项目作者声明已补齐；公开分发前仍需核对字体资源的许可材料，并确保新增源码全部纳入提交。
本次验证不等于在全新操作系统上完成所有平台的安装验收。

## 本次修正

- 增加 `.gitattributes`：普通源文件和脚本使用 LF，Windows CMD 使用 CRLF。
  字节校验的第三方源码、已发布包源码/资源、播放器及注册表文件保留原始字节。
  不能将全部第三方源码一律转成 LF：SoftFloat 的固定源码本来使用 CRLF。
  `sCrpp.toml` 同样保留字节，避免 Git 改变锁文件记录的清单 SHA-256。
  独立模板也附带相同的清单换行约定及包缓存忽略规则。
  新规则可能让原已被 Git 规范化的第三方文件显示为修改，这是恢复校验所需的原始字节。
- CMake 安装增加 `share/scratch-llvm/template/` 和完整 docs；模板安装排除本机配置、
  build、包缓存。安装目录可独立复制模板。
- 修正模板及说明中的过期内容：注册表依赖、Console 0.3.0、iostream、内嵌播放器、
  Node/LLDB 要求；明确 pip 仅安装 scrate，不等于安装编译器与 SDK。
- 更新模板清单注释及锁指纹；修复 iostream 示例清单已升级而锁文件仍为旧 Console 的问题。
- 将 2026-09-13 的 Linux/Windows 验证标明为历史记录，不冒充当前版本的完整跨平台测试。

## 本次实际验证

1. 使用**独立临时 Git index**收集已跟踪及未忽略的新文件，在 `core.autocrlf=true`
   条件下模拟检出；没有修改用户暂存区。候选内容约 2708 个文件、31.5 MiB，最大单文件
   为 PTE 字库约 6.2 MiB。
2. 检出副本通过全部 2314 个固定源码 SHA-256 检查：libc++ 1736、LLVM libc 146、
   musl 3、SoftFloat 429。模板和 iostream 示例锁文件的清单指纹一致。
3. 对候选文件扫描私钥头、GitHub token、AWS access key 常见格式，未命中；对
   tools/cmake/debugger/template 搜索开发者本机用户路径，未发现。这是有限模式检查，
   不代表完成全面凭据审计或扫描 Git 历史。
4. 当前 Windows CMake 配置和构建成功。13 组资源、模板、包管理、工具发现、调试传输、
   CLI 和播放器状态相关 CTest 全部通过。
5. 从线上注册表用独立空缓存获取 Console 0.3.0、Events 0.1.0、PTE 0.1.0、
   Triangle 0.1.1，并通过索引 SHA-256 校验。
6. CMake 安装到仓库外临时目录，复制**安装的模板**到独立项目，清除 SCRATCH_/SCRATE_
   环境覆盖，只显式提供安装目录和准备好的 Clang SDK。使用该独立包缓存执行
   `scrate build --offline --locked` 的 Release / Debug 两种构建，均成功；Debug 映射存在。
7. 安装目录内运行 `debugger/pack_extension.py`，成功生成扩展 0.1.8 的 VSIX。

原始记录在 `build/validation/repository-readiness/`，包括 source-audit.json、
install-audit.json、install.log、release.log、debug.log、vsix.log。它们是本机验证产物，不入库。

本次未重新执行完整 CTest、真实 VS Code/LLDB 交互、Linux/macOS 全量构建，也未在检出副本
重新完整编译编译器。隔离构建仍使用本机 Python 和准备好的 Clang，不是零依赖系统验收。
已有功能验证见 [iostream](IOSTREAM.md)、[控制台](CONSOLE_RESULTS.md) 和 [调试器](../debugger/README.md)。

## 上传前仍需处理

### 1. 主项目及资源许可

仓库根目录已按维护者决定添加 MIT LICENSE，署名使用 Git 用户名 thedifferentman 及项目贡献者。
第三方内容不自动适用 MIT。来源和作者原文见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。

以下材料只有来源或部分许可说明，尚未记录完整分发授权依据：

- PTE 字库包含 Consolas、微软雅黑转换后的字形及原始 PTE 回退字形。
  未附原始字体文件，不等于已说明转换字形的分发许可。
- PTE 页面已确认允许注明来源后使用；C1 页面欢迎引用，作者也回复过“不同之者”的引用请求。
  原包 SOURCE.json 是此前离线文件检查的记录；新取得的页面依据另记在根第三方声明中，未覆盖已发布包。
- 原 PTE 页面提到思源黑体，仍需核对离线字形数据对应版本及随附许可。
- 转换工具和主项目的 MIT 声明不覆盖第三方字体数据。

应补齐相应许可/授权材料，或替换成授权明确的素材。这里记录材料缺口，不对具体字体的
分发权作法律判断。还应在发行附件中保留 LLVM、libc++、SoftFloat、musl、播放器等随附许可。

### 2. 提交完整工作区

当前必要的新文件仍有很多显示为 `??`，包括包、scrate 模块、播放器、运行库源文件、文档、
测试及网站。仅提交 tracked 文件的修改会造成缺文件仓库。
在 IDE 中查看“未跟踪文件”，将需要交付的源码纳入提交，并检查暂存清单。

第三方源码和字库是实际输入，不应因数量多而一律忽略。`website/dist/` 是当前静态站点及
注册表内容，特意保留；根 `build/`、缓存、VSIX、生成 SB3 和本机配置继续忽略。
本次没有替用户暂存或提交。

### 3. 清楚区分源码与二进制发行

源码仓库提供准备脚本和构建输入，不包含已编译的 Clang、scratch-llvm、SDK bitcode、VSIX。
用户按快速入门自行准备和构建即可。若希望普通用户免构建，后续另提供对应平台的完整
安装压缩包、VSIX、校验值和版本说明，而不是只上传 scratch-llvm.exe。

当前版本组合：编译器 0.1.0、scrate 0.2.0、SDK ABI 2、扩展 0.1.8、Console 0.3.0。
Windows 有本次安装验证；Linux 完整宿主记录对应 2026-09-13；macOS 尚未实机验收。
Linux/macOS 宿主共享库仍依赖配置的系统/SDK路径，不能宣传任意环境免依赖运行。

## 可以后续改进

- 上传后配置 Windows/Linux CI：至少从干净检出开始完成固定源码校验、构建、安装和独立模板构建。
- 增加集中版本发布说明及 issue 模板，收集操作系统、工具版本、清单、命令和错误日志。
- 首版明确 Alpha 范围：禁用异常/RTTI/线程，暂无文件系统和容器调试美化，输入限 ASCII。
  这些功能缺失不必阻塞开发者试用；不要将其描述为完整 C++ 平台。
