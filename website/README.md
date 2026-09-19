# scrate 包仓库网站

纯静态页面，无前端构建依赖、账号系统或上传接口。目前提供本地已有的
Console 0.3.0、Triangle 0.1.1、Events/PTE 0.1.0 源码包（保留 Console 0.1.0/0.2.0），并保留 scrate 静态索引协议。
现由用户的静态服务提供 `dist`，通过 Cloudflare Tunnel 对外开放为 `scrate.shapy.cn`。

## 本地预览

在项目根目录运行：

```sh
python -m http.server 8173 --bind 127.0.0.1 --directory website/dist
```

打开 <http://127.0.0.1:8173/>。在需要安装包的项目中，可指定此仓库：

```sh
scrate install --registry http://127.0.0.1:8173
```

页面上的默认安装命令可直接使用正式域名；本地测试需使用上述地址。

## 文件结构与部署

- `dist/index.html`：包目录、下载链接、依赖说明和版本校验值。
- `dist/styles.css`、`dist/site.js`：响应式样式及复制按钮。
- `dist/blue-c-icon.svg`：提供的原始 SVG，用于导航标志与标签页图标；页面采用蓝色积木风格。
- `dist/index/<包名>/<版本>.json`：scrate 客户端读取的版本索引。
- `dist/packages/<包名>/<版本>/<包名>-<版本>.zip`：实际下载包。

部署时将 **整个 `dist` 的内容** 放到站点根目录，保留路径和 ZIP 原始字节。
使用普通静态文件服务；不存在的索引必须返回 404，不要配置将所有请求重写到
`index.html` 的 SPA 回退。网站没有跨域请求或服务端运行时要求。

## 更新包

当前 ZIP 和版本索引原样复制自 `build/scrate-registry`，未重新打包。
添加新版本时，先更新库清单的版本及依赖，再从项目根目录执行，例如：

```sh
scrate pack --manifest include/events/sCrpp.toml --output-dir website/dist
```

依赖包也必须存在于此站点。随后更新 `dist/index.html` 中的版本、依赖声明、
下载路径、文件大小及 SHA-256；校验值应与生成的 JSON 一致。
已发布的相同包名与版本应保持不可变，修改内容需要发布新版本。

## 已验证

- 三个实际 HTTP 下载文件的 SHA-256 与索引及页面一致。
- scrate 能通过本地 HTTP 索引安装 Console 及其两个传递依赖。
- 已缓存后可使用锁文件离线解析，锁文件保持不变。
- 复制依赖、展开校验信息正常；桌面及 320/390 像素窄屏排版正常。

本次验证报告及截图保存在仓库的 `build/validation/scrate-website/`（不入库）。
