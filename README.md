# MineFetch

一个用 C++17 编写的 Minecraft Java 版命令行下载器，默认通过 **BMCLAPI** 下载，Windows 10/11 x64 可用。

GitHub 仓库：<https://github.com/XiaLize/MineFetch>

程序只负责补齐游戏文件，不包含微软登录、JVM 启动和 `natives` 解压。下载完成后可以直接交给 HMCL、PCL 等启动器使用，或者在此基础上继续编写启动器。

> BMCLAPI 下的文件版权归原站点所有。本程序在下载界面明确显示当前下载源；请遵守 [BMCLAPI 文档](https://bmclapidoc.bangbang93.com/) 的使用规范，不要拿它二次封装其他协议。

## 功能

- 默认 BMCLAPI，也可以切换 Mojang 官方源或任意兼容镜像。
- 双击或直接运行时会弹出 Windows 文件夹选择窗口，用来选择 `.minecraft` 目录。
- 下载版本清单、版本 JSON、客户端 jar、依赖库、日志配置、资源索引和全部资源对象。
- 解析 `rules` 中的 Windows / 架构 / feature 条件。
- 支持旧版 `natives` / `classifiers` 映射，以及旧版 `virtual`、`map_to_resources` 资源目录。
- 所有文件按官方 SHA-1 和大小校验；校验失败自动重下。
- 8 线程并发，进度条显示速度、剩余文件数和失败数。
- 已完成的文件会跳过；损坏文件会自动识别并重新下载。
- 下载先写 `.part`，校验成功后再原子替换，避免留下半个 jar。
- 自动跟随 302 跳转，处理 `429` 的 `Retry-After-short` 限流响应。
- BMCLAPI 请求失败时，默认自动切换 Mojang 官方源重试（`--no-fallback` 可关闭）。
- `list` / `info` 同样会在 BMCLAPI 不可用时快速回退官方源，不会卡在 30 秒连接超时或 45 秒限流等待上。
- 交互模式和命令行都会先输出“正在获取版本清单”，避免出现无反馈的等待。
- 无第三方运行时依赖：HTTP 使用 WinHTTP，SHA-1 使用 Windows CNG，JSON 解析器自带。

## 构建

克隆仓库：

```bat
git clone git@github.com:XiaLize/MineFetch.git
cd MineFetch
```

### 方式一：MinGW-w64（推荐，已验证 g++ 11.5）

代码目录按你的环境使用了 `C:\mingw64`：

```bat
build.bat
```

或者手动：

```bat
set PATH=C:\mingw64\bin;%PATH%
mingw32-make -j8
```

产物为根目录下的 `minefetch.exe`。Makefile 使用静态链接，不依赖 MinGW DLL。

### 方式二：CMake + Visual Studio

```bat
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Release
```

产物为 `build-vs\Release\minefetch.exe`。CMake 已把 MSVC 运行库设为静态链接。

要求：Windows 10/11、x64、支持 C++17 的编译器。链接 `winhttp`、`bcrypt`、`ole32`、`uuid` 和 `shell32`。

## 使用

```text
minefetch install <版本> [选项]
minefetch <版本> [选项]
minefetch list [选项]
minefetch info <版本> [选项]
minefetch
```

不带参数运行会先弹出文件夹选择窗口，然后进入交互模式；直接双击 `minefetch.exe` 也可以。

从命令行运行 `minefetch install ...` 且没有指定 `--dir` 时，同样会弹窗选择目录；脚本或自动化场景请加 `--no-pick`，或直接用 `--dir` 指定路径。取消弹窗时会使用默认目录 `%APPDATA%\.minecraft`。

### 常用示例

```bat
:: 下载最新正式版到默认目录 %APPDATA%\.minecraft
minefetch install latest

:: 下载 1.20.1 到独立目录
minefetch install 1.20.1 --dir D:\Games\.minecraft

:: 只补齐客户端、依赖库和日志配置，不下载 600MB+ 的资源
minefetch install 1.20.1 --no-assets

:: 查看前 20 个正式版
minefetch list --limit 20

:: 查看所有快照
minefetch list --type all --limit 0

:: 查看版本信息和 SHA-1
minefetch info 1.21.4

:: 使用 Mojang 官方源
minefetch install 1.20.1 --mirror mojang

:: 使用兼容 BMCLAPI 的私有镜像
minefetch install 1.20.1 --mirror-url https://mirror.example.com
```

### 全部选项

| 选项 | 说明 |
| --- | --- |
| `-d, --dir <目录>` | 游戏目录，默认 `%APPDATA%\.minecraft` |
| `--pick-dir` | 强制弹窗选择 `.minecraft` 文件夹 |
| `--no-pick` | 不自动弹窗，直接使用默认或 `--dir` 指定的目录 |
| `-m, --mirror <源>` | `bmclapi`（默认）或 `mojang` |
| `--mirror-url <URL>` | 自定义 BMCLAPI 兼容镜像根地址 |
| `-t, --threads <1-32>` | 并发线程数，默认 8 |
| `-f, --force` | 忽略已有文件，全部重新下载 |
| `--no-assets` | 不下载 assets |
| `--no-logging` | 不下载日志配置 |
| `--no-verify` | 跳过 SHA-1 校验（更快，但可能保留损坏文件） |
| `--no-fallback` | BMCLAPI 失败时不自动切换官方源 |
| `--retries <0-10>` | 单个请求失败后的重试次数，默认 3 |
| `--timeout <5-300>` | 网络超时秒数，默认 30 |
| `-v, --verbose` | 输出每个正在下载的文件 |
| `--type <类型>` | `list` 过滤：`release`、`snapshot`、`all` |
| `--limit <数量>` | `list` 显示数量，`0` 为全部，默认 30 |

## 下载后的目录

以 `--dir D:\Games\.minecraft`、版本 `1.20.1` 为例：

```text
D:\Games\.minecraft\
├─ versions\1.20.1\
│  ├─ 1.20.1.json
│  └─ 1.20.1.jar
├─ libraries\
│  └─ ...（按 Maven 路径存放）
├─ assets\
│  ├─ indexes\5.json
│  ├─ objects\ab\abcdef...
│  └─ virtual\legacy\...（仅旧版资源索引需要）
├─ resources\...（仅 map_to_resources 的旧版索引需要）
└─ logging\client-1.12.xml
```

## BMCLAPI 映射

程序读取的版本 JSON 里仍然可能是 Mojang 官方 URL，因此下载前会统一改写：

| 官方前缀 | BMCLAPI |
| --- | --- |
| `https://piston-meta.mojang.com` | `https://bmclapi2.bangbang93.com` |
| `https://launchermeta.mojang.com` | `https://bmclapi2.bangbang93.com` |
| `https://launcher.mojang.com` | `https://bmclapi2.bangbang93.com` |
| `https://piston-data.mojang.com` | `https://bmclapi2.bangbang93.com` |
| `https://libraries.minecraft.net` | `https://bmclapi2.bangbang93.com/maven` |
| `https://resources.download.minecraft.net` | `https://bmclapi2.bangbang93.com/assets` |

版本清单使用：

```text
https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json
```

客户端在缺少 `downloads.client.url` 时还会尝试：

```text
https://bmclapi2.bangbang93.com/version/<版本>/client
```

BMCLAPI 会先返回 302，再跳到 OpenBMCLAPI 节点。程序使用 WinHTTP 自动跟随跨域跳转；节点偶尔会重置连接，重试会重新获取跳转地址。

### 关于限流

实测 BMCLAPI 会在短时间内返回：

```http
HTTP/1.1 429 Too Many Requests
Retry-After-short: 45
```

程序会读取 `Retry-After-short` 并等待，随后重试。大批量资源下载时 BMCLAPI 可能较慢；如果多次失败，默认会自动切到 Mojang 官方源，也可以手动加 `--mirror mojang`。`list` / `info` 在默认配置下会直接快速回退，不会反复等待限流。

## 实现结构

| 文件 | 作用 |
| --- | --- |
| `src/main.cpp` | 命令行解析、交互模式、`list` / `info`、自检 |
| `src/folder_dialog.cpp` | Windows 文件夹选择窗口（`IFileOpenDialog`） |
| `src/installer.cpp` | 下载计划、SHA-1 扫描、并发队列、进度、旧版资源复制 |
| `src/remote.cpp` | 版本清单/版本 JSON 获取与本地缓存回退 |
| `src/minecraft.cpp` | 版本 JSON、libraries、rules、assets index 解析 |
| `src/mirror.cpp` | BMCLAPI / Mojang URL 映射 |
| `src/http.cpp` | WinHTTP 封装、跳转、重试、429、超时、流式下载 |
| `src/json.cpp` | 自带的小型 JSON 解析器 |
| `src/sha1.cpp` | Windows CNG SHA-1 |
| `src/common.cpp` | UTF-8/宽字符路径、终端、格式化 |

### 下载流程

1. 获取 `version_manifest_v2.json`，解析最新版本和版本列表。
2. 获取版本 JSON，按 SHA-1 校验并保存到 `versions/<id>/<id>.json`。
3. 解析 `downloads.client`、`libraries`、`logging` 和 `assetIndex`。
4. 计算 Windows 平台真正需要的依赖库，包括旧版 natives classifier。
5. 拉取资源索引，生成每个资源对象的任务。
6. 扫描本地文件：大小一致且 SHA-1 正确就跳过。
7. 多线程下载，先写 `.part`，校验通过后替换目标文件。
8. 为旧版索引生成 `assets/virtual/legacy` 或 `resources` 副本。

## 测试

无网络自检：

```bat
minefetch.exe --self-test
```

本地镜像端到端测试：

```powershell
# 终端 1：生成 fixture 并启动本地 HTTP 服务
powershell -ExecutionPolicy Bypass -File tests\make_fixture.ps1
powershell -ExecutionPolicy Bypass -File tests\serve_fixture.ps1

# 终端 2：下载
.\minefetch.exe install testver --mirror-url http://127.0.0.1:8765 --dir .\testdata-local -t 4 -v
```

fixture 覆盖了版本清单、302 跳转、客户端、依赖库、日志、资源索引、资源对象、SHA-1 校验和旧版资源目录复制。

## 限制

- 只支持独立原版版本；遇到 `inheritsFrom`（Forge / NeoForge / OptiFine 等）会停止并提示。
- 不下载 Java 运行时，不安装 Java，不写 `launcher_profiles.json`。
- 不启动游戏，不处理微软登录。
- 不把 `natives` jar 解压到 `natives` 目录；这一步属于启动器的工作。
- 不下载服务端 jar。
- 网络使用 WinHTTP 的默认代理设置。若需要代理，可先执行：

  ```bat
  netsh winhttp set proxy 127.0.0.1:7890
  netsh winhttp reset proxy
  ```

## 致谢与来源

- [BMCLAPI 文档](https://bmclapidoc.bangbang93.com/)
- [Minecraft Wiki：教程/编写启动器](https://zh.minecraft.wiki/w/Tutorial:%E7%BC%96%E5%86%99%E5%90%AF%E5%8A%A8%E5%99%A8)
- [BMCLAPI 主站](https://bmclapi2.bangbang93.com/)

Minecraft 是 Mojang Synergies AB 的商标；本项目与 Mojang / Microsoft 无隶属关系。

## License

[MIT](LICENSE)
