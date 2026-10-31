# MineFetch 开发与构建

本文面向要修改、构建或发布 MineFetch 的开发者。普通用户请阅读 [README.md](README.md)。

## 技术栈

- C++17
- WinHTTP：HTTP/HTTPS、系统代理、302 跳转、限流响应
- Windows CNG（bcrypt）：SHA-1
- Win32 / COM：UTF-16 路径、文件夹选择窗口
- 自带 JSON 解析器，不依赖第三方库
- 构建产物为单个 `minefetch.exe`，只依赖 Windows 系统 DLL

## 构建环境

- Windows 10/11 x64
- MinGW-w64 g++ 11.5 或 Visual Studio 2022
- CMake 3.20+（使用 Visual Studio 构建时）

链接的系统库：

```text
winhttp bcrypt ole32 uuid shell32
```

## MinGW 构建

`build.bat` 会优先使用 `C:\mingw64\bin`：

```bat
build.bat
```

手动构建：

```bat
set PATH=C:\mingw64\bin;%PATH%
mingw32-make -j8
```

产物：

```text
minefetch.exe
```

Makefile 使用 `-static -static-libgcc -static-libstdc++`，生成的 exe 不依赖 MinGW DLL。

## Visual Studio / CMake 构建

```bat
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Release
```

产物：

```text
build-vs\Release\minefetch.exe
```

CMake 将 MSVC 运行库设为静态链接。

## 运行自检

```bat
minefetch.exe --self-test
```

自检覆盖 JSON 解析、SHA-1、镜像 URL 映射、Maven 路径和版本清单解析，不需要网络。

## 本地镜像端到端测试

测试脚本位于 `tests/`，会生成一个很小的假版本，覆盖：

- 版本清单、版本 JSON
- 302 跨路径跳转
- 一次 429 `Retry-After-short` 响应
- 客户端、依赖库、日志配置、资源索引、资源对象
- SHA-1 校验
- `virtual` / `map_to_resources` 旧版资源目录

终端 1：

```powershell
powershell -ExecutionPolicy Bypass -File tests\make_fixture.ps1
powershell -ExecutionPolicy Bypass -File tests\serve_fixture.ps1
```

终端 2：

```powershell
.\minefetch.exe install testver --mirror-url http://127.0.0.1:8765 --dir .\testdata-local -t 4 -v
```

再次执行同一命令应跳过所有文件。修改客户端 jar 后重跑，应只重新下载损坏文件。

## 目录结构

```text
src/
├─ main.cpp           命令行、交互模式、list/info、自检
├─ folder_dialog.cpp  Windows 文件夹选择窗口
├─ installer.cpp      下载计划、SHA-1 扫描、并发队列、进度、旧版资源复制
├─ remote.cpp         版本清单/版本 JSON 获取与本地缓存回退
├─ minecraft.cpp      版本 JSON、libraries、rules、assets index 解析
├─ mirror.cpp         BMCLAPI / Mojang URL 映射
├─ http.cpp           WinHTTP 封装、跳转、重试、429、流式下载
├─ json.cpp           自带 JSON 解析器
├─ sha1.cpp           Windows CNG SHA-1
└─ common.cpp         UTF-8/宽字符路径、终端、格式化
```

## 下载流程

1. 获取 `version_manifest_v2.json`，解析最新版本和版本列表。
2. 获取版本 JSON，按 SHA-1 校验，保存到 `versions/<id>/<id>.json`。
3. 解析 `downloads.client`、`libraries`、`logging`、`assetIndex`。
4. 计算当前 Windows 平台需要的依赖库，包括旧版 `natives` classifier。
5. 拉取资源索引，生成每个资源对象的任务。
6. 扫描本地文件：大小一致且 SHA-1 正确则跳过。
7. 多线程下载，先写 `.part`，校验通过后替换目标文件。
8. 为旧版索引生成 `assets/virtual/legacy` 或 `resources` 副本。

## BMCLAPI 映射

版本 JSON 中的 URL 可能是 Mojang 官方地址，下载前统一改写：

| 官方前缀 | BMCLAPI |
| --- | --- |
| `https://piston-meta.mojang.com` | `https://bmclapi2.bangbang93.com` |
| `https://launchermeta.mojang.com` | `https://bmclapi2.bangbang93.com` |
| `https://launcher.mojang.com` | `https://bmclapi2.bangbang93.com` |
| `https://piston-data.mojang.com` | `https://bmclapi2.bangbang93.com` |
| `https://libraries.minecraft.net` | `https://bmclapi2.bangbang93.com/maven` |
| `https://resources.download.minecraft.net` | `https://bmclapi2.bangbang93.com/assets` |

版本清单：

```text
https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json
```

缺少 `downloads.client.url` 时的回退：

```text
https://bmclapi2.bangbang93.com/version/<版本>/client
```

## HTTP 行为

- 使用 `WinHttpOpen` 的默认代理配置，不读取 IE 的自动代理/PAC。
- 允许跨主机 302，但禁止 HTTPS 降级到 HTTP。
- 对超时、连接重置、5xx 等可重试错误做指数退避。
- 读取 429 的 `Retry-After-short` 或 `Retry-After`，等待后重试。
- 所有下载先写 `<目标>.part`，校验 SHA-1/大小后 `MoveFileEx` 原子替换。
- 失败或取消时删除 `.part`，不会留下半个 jar。

## 版本元数据支持

- `version_manifest_v2.json`
- `downloads.client`
- `libraries[].downloads.artifact`
- `libraries[].downloads.classifiers` + `natives`（旧版）
- `rules` 的 `os.name`、`os.arch`、`features`
- `logging.client.file`
- `assetIndex` 及所有 `objects`
- 旧版 `virtual`、`map_to_resources`
- 检测到 `inheritsFrom` 时拒绝，避免生成不完整的 Forge/OptiFine 版本

## 发布

当前 Release 使用 MinGW 静态构建：

```bat
build.bat
```

发布资产只包含 `minefetch.exe`，源码通过 GitHub 自动生成的 zip/tar.gz 提供。

`minefetch.exe` 在 `.gitignore` 中，不提交到源码仓库。

## 代码约定

- 不使用第三方运行时库。
- 所有文件路径使用 `std::filesystem::path` 或 UTF-16，不把中文路径转成 ANSI。
- HTTP 只在工作线程创建 `HttpClient`，不要跨线程共享同一实例。
- 新增文件类型时同时考虑 SHA-1、大小未知、取消和重试。
- 新增网络请求时确认不会破坏 `--no-fallback` 和 `--no-pick` 的行为。

## 许可

MIT，见 [LICENSE](LICENSE)。Minecraft 是 Mojang Synergies AB 的商标，本项目与 Mojang / Microsoft 无隶属关系。
