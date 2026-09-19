# 基础 SDK 与 scrate 包

基础 SDK 自动提供 libc++ 头文件、基础 C/C++ 运行库和目标 ABI 信息。
Console、Events、PTE、Triangle 现在是普通 scrate 包，通过项目 sCrpp.toml 的 [dependencies] 获取。
它们不再自动并入 SDK，也不会在无依赖程序中附带字库或事件帽子。

完整使用方式、静态索引协议和离线构建见 [scrate 文档](SCRATE.md)。

升级旧工作区时，重新运行 `cmake --build --preset clang` 更新基础 SDK 和工具。
SDK 构建会清理旧的可选库头文件、资源和 scrpp-stdlib.bc。然后使用新模板构建脚本，
声明包依赖并生成 scrate.lock。模板已配置注册表 Console 0.3.0 依赖；首次构建联网下载，缓存齐全后支持离线构建。

验证入口为 scrate、scrate-pipeline 和 template-build CTest；
`tests/scrate_pipeline_test.py` 覆盖真实包、传递依赖、源码与头文件路径、
Release/Debug、断网锁定构建及无依赖项目。
