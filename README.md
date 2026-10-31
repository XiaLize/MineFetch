# MineFetch

MineFetch 是一个 Windows 上的 Minecraft Java 版游戏文件下载器。它可以自动补齐客户端、依赖库、资源文件和日志配置，省去手动翻 `versions`、`libraries`、`assets` 目录的麻烦。

默认使用国内可访问的 **BMCLAPI**，下载失败时会自动切换到 Mojang 官方源。

**下载最新版：** [MineFetch Releases](https://github.com/XiaLize/MineFetch/releases/latest)

**直接下载 exe：** [minefetch.exe](https://github.com/XiaLize/MineFetch/releases/latest/download/minefetch.exe)

> MineFetch 是下载器，不是启动器。它不登录微软账号、不启动游戏、不安装 Java。下载完成后，请用 HMCL、PCL 等启动器打开对应的 `.minecraft` 文件夹。

## 适合什么场景

- 想补全一个已有的 `.minecraft` 目录
- 国内网络环境，希望优先走 BMCLAPI
- 不想手动下载几十个依赖库和几千个资源文件
- 需要下载旧版本或快照
- 希望文件完整性有校验，而不是只看“下载完成”

## 快速开始

### 1. 下载程序

从 [Releases](https://github.com/XiaLize/MineFetch/releases/latest) 下载 `minefetch.exe`。

程序是绿色版，不需要安装，也没有注册表写入。放到任意文件夹即可运行。

> `minefetch.exe` 没有做代码签名，Windows SmartScreen 可能提示“Windows 已保护你的电脑”。如果确认文件来自本仓库，可以点“更多信息”再点“仍要运行”。

### 2. 双击运行

程序会先弹出文件夹选择窗口：

- 选择已有的 `.minecraft` 文件夹，或
- 在窗口里新建一个 `.minecraft` 文件夹

如果取消选择，会使用默认目录：

```text
%APPDATA%\.minecraft
```

### 3. 输入版本

窗口关闭后，在控制台输入版本号，例如：

```text
1.20.1
```

直接按回车会下载最新正式版。输入 `list` 可以查看版本，输入 `q` 退出。

### 4. 等待完成

程序会显示下载进度、速度、文件数量和失败数。下载完成后会按官方 SHA-1 校验文件。

### 5. 用启动器打开

在 HMCL、PCL 等启动器中把游戏目录指向刚才选择的 `.minecraft` 文件夹，选择对应版本即可启动。

## 它能做什么

- 下载客户端 jar、版本 JSON、依赖库、日志配置、资源索引和全部资源对象
- 支持正式版、快照和旧版本
- 自动处理 Windows 平台相关的依赖规则
- 支持旧版 `natives` 和旧版资源目录
- 有官方 SHA-1 的文件都会校验，损坏文件自动重新下载
- 多线程下载，显示实时进度
- 中断后重新运行会跳过已完成文件
- 默认 BMCLAPI，失败自动回退 Mojang 官方源
- 处理 BMCLAPI 的 429 限流，等待后重试
- 支持命令行和双击交互两种方式

## 它不能做什么

MineFetch 只负责“把游戏文件下载完整”，以下事情不包含：

- 不登录微软账号
- 不启动游戏
- 不安装或管理 Java
- 不写 `launcher_profiles.json`
- 不解压 `natives`（启动器会在启动时处理）
- 不下载服务端 jar
- 不支持 `inheritsFrom` 的整合版本，例如 Forge、NeoForge、OptiFine 的版本 JSON

如果你要安装 Forge、Fabric 或整合包，请使用对应的启动器或安装器。

## 命令行使用

如果不想用交互模式，可以直接在命令提示符或 PowerShell 中运行。

```bat
:: 下载最新正式版（会弹窗选择目录）
minefetch.exe install latest

:: 直接下载到默认目录，不弹窗
minefetch.exe install latest --no-pick

:: 下载 1.20.1 到指定目录
minefetch.exe install 1.20.1 --dir D:\Games\.minecraft

:: 查看正式版列表
minefetch.exe list

:: 查看所有版本
minefetch.exe list --type all --limit 0

:: 查看某个版本的信息
minefetch.exe info 1.21.4

:: 只下载核心文件，不下载资源
minefetch.exe install 1.20.1 --no-pick --no-assets

:: 使用 Mojang 官方源
minefetch.exe install 1.20.1 --no-pick --mirror mojang
```

### 目录选择

| 场景 | 行为 |
| --- | --- |
| 双击运行 | 自动弹窗选择 `.minecraft` |
| 命令行安装且没有 `--dir` | 自动弹窗选择 `.minecraft` |
| 取消弹窗 | 使用 `%APPDATA%\.minecraft` |
| 指定 `--dir` | 直接使用指定目录，不弹窗 |
| `--pick-dir` | 强制弹窗 |
| `--no-pick` | 不弹窗，适合脚本和自动化 |

例如脚本中不想弹窗：

```bat
minefetch.exe install 1.20.1 --dir D:\Games\.minecraft --no-pick
```

### 静默安装

适合批处理、计划任务或远程执行。`--silent` 会：

- 不弹文件夹选择窗口
- 不显示进度和普通提示
- 只在出错时输出
- 如果没有指定 `--dir`，直接使用 `%APPDATA%\.minecraft`

```bat
minefetch.exe install 1.20.1 --dir D:\Games\.minecraft --silent
```

成功时退出码为 `0`，失败时为 `1`，取消时为 `130`。

`--silent` 只用于 `install`，不能和 `list` / `info` 一起使用。

### 自定义版本名

默认情况下，版本会保存为 `versions\<原版版本号>\`。如果希望启动器里显示自定义名称，可以使用 `--name`：

```bat
minefetch.exe install 1.20.1 --name "我的1.20.1" --dir D:\Games\.minecraft
```

下载后会生成：

```text
.minecraft\versions\我的1.20.1\
├─ 我的1.20.1.json
└─ 我的1.20.1.jar
```

说明：

- 只生成这一个版本文件夹，不会额外创建 `versions\1.20.1\`
- 版本 JSON 里的 `id` 也会改成自定义名称，启动器会显示这个名字
- 不能使用官方版本号作为自定义名称，例如已经有 `1.20.1` 时不能再叫 `1.20.1`
- 名称不能包含 `\ / : * ? " < > |` 等 Windows 非法字符
- 如果目标名称已存在且属于其他版本，程序会拒绝覆盖
- 可以和 `--silent` 一起使用

```bat
minefetch.exe install 1.20.1 --name "整合包-1.20.1" --dir D:\Games\.minecraft --silent
```

### 常用选项

| 选项 | 说明 |
| --- | --- |
| `-d, --dir <目录>` | 指定游戏目录 |
| `--pick-dir` | 弹窗选择 `.minecraft` 文件夹 |
| `--no-pick` | 不自动弹窗 |
| `-m, --mirror <源>` | `bmclapi`（默认）或 `mojang` |
| `--mirror-url <URL>` | 使用兼容 BMCLAPI 的自定义镜像 |
| `-t, --threads <1-32>` | 并发线程数，默认 8 |
| `-f, --force` | 忽略已有文件，全部重新下载 |
| `-s, --silent` | 静默安装，不弹窗、不输出进度，只显示错误 |
| `--name <名称>` | 自定义版本名，生成独立的版本文件夹 |
| `--no-assets` | 不下载资源文件 |
| `--no-logging` | 不下载日志配置 |
| `--no-verify` | 跳过 SHA-1 校验，不建议使用 |
| `--no-fallback` | BMCLAPI 失败时不自动切换官方源 |
| `--retries <0-10>` | 单个请求失败后的重试次数，默认 3 |
| `--timeout <5-300>` | 网络超时秒数，默认 30 |
| `-v, --verbose` | 显示每个正在下载的文件 |
| `--type <类型>` | `list` 过滤：`release`、`snapshot`、`all` |
| `--limit <数量>` | `list` 显示数量，`0` 表示全部 |

## 下载后的文件

以选择 `D:\Games\.minecraft`、下载 `1.20.1` 为例：

```text
D:\Games\.minecraft\
├─ versions\1.20.1\
│  ├─ 1.20.1.json
│  └─ 1.20.1.jar
├─ libraries\
│  └─ ...（游戏依赖库）
├─ assets\
│  ├─ indexes\
│  ├─ objects\
│  └─ virtual\legacy\...（仅旧版需要）
├─ resources\...（仅部分旧版需要）
└─ logging\client-1.12.xml
```

不同版本会共享 `libraries` 和 `assets`，所以下载第二个版本时通常只需要补少量文件。

## 常见问题

### 双击后没有窗口

1. 等 1～2 秒，程序需要先初始化。
2. 确认没有加 `--no-pick` 参数。
3. 如果通过脚本或重定向输出运行，Windows 不会把它当作交互终端，此时不会自动弹窗，请手动加 `--pick-dir`，或用 `--dir` 指定目录。
4. 如果 SmartScreen 拦住了程序，点“更多信息”再点“仍要运行”。

### 提示 BMCLAPI 失败

程序会自动切换到 Mojang 官方源，通常不需要手动处理。

如果想直接使用官方源：

```bat
minefetch.exe install 1.20.1 --no-pick --mirror mojang
```

如果两个源都失败，请检查网络、DNS 或代理设置。

### 提示 HTTP 429 Too Many Requests

BMCLAPI 在短时间内请求过多会限流。程序会读取等待时间并重试。

如果不想等，可以：

```bat
minefetch.exe install 1.20.1 --no-pick --mirror mojang
```

或者等一分钟左右再运行。

### 下载中断了怎么办

重新运行同一条命令即可。已经完整且校验通过的文件会跳过，损坏的文件会重新下载。

例如：

```bat
minefetch.exe install 1.20.1 --dir D:\Games\.minecraft
```

### 游戏缺少声音、语言或材质

可能是用了 `--no-assets`，或者资源没有下载完整。去掉 `--no-assets` 重新运行：

```bat
minefetch.exe install 1.20.1 --dir D:\Games\.minecraft
```

### 下载后启动器里没有版本

确认启动器指向的是正确的 `.minecraft` 目录，而不是它的上级目录。

正确：

```text
D:\Games\.minecraft
```

错误：

```text
D:\Games
```

### 启动器提示缺少 Java

MineFetch 不安装 Java。请安装对应版本需要的 Java，或使用自带 Java 管理功能的启动器。

例如 1.20.1 通常需要 Java 17，较新的版本可能需要 Java 21 或更高。

### 启动游戏时报 natives 相关错误

MineFetch 只下载 natives jar，不解压。请让启动器负责解压，或重新用启动器启动一次该版本。

### 下载很慢

可以尝试：

```bat
minefetch.exe install 1.20.1 --threads 12 --no-pick
```

线程不是越多越好。网络不稳定时，保持默认 8 或更低通常更稳。

### 需要走代理

程序使用 WinHTTP 的默认代理设置。可以先设置：

```bat
netsh winhttp set proxy 127.0.0.1:7890
```

取消代理：

```bat
netsh winhttp reset proxy
```

### 想强制重新下载

```bat
minefetch.exe install 1.20.1 --dir D:\Games\.minecraft --force
```

这会重新下载所有文件，包括已经完整的文件。一般不需要。

### 如何更新 MineFetch

下载新版本的 `minefetch.exe`，替换旧文件即可。程序没有配置文件，也不需要迁移数据。

### 如何卸载

MineFetch 不写注册表、不写系统目录。删除 `minefetch.exe` 即可。

下载到 `.minecraft` 里的游戏文件不会自动删除；如果不需要，手动删除对应目录。

## 安全与隐私

- 不要求微软账号，不读取账号信息
- 不包含遥测或统计上报
- 只访问版本清单、依赖库和资源下载地址
- 提供官方 SHA-1 的文件都会校验
- 下载先写 `.part`，校验成功后才替换目标文件
- 源码公开，可自行审查或构建

BMCLAPI 只作为下载镜像使用，游戏文件版权归原站点所有。使用 BMCLAPI 时请遵守其使用规范。

## 系统要求

- Windows 10 或 Windows 11，x64
- 建议至少预留 1 GB 磁盘空间（现代版本完整资源约 700 MB，旧版本更小）
- 首次运行不需要管理员权限

## 获取帮助

- 先查看上面的常见问题
- 在 [GitHub Issues](https://github.com/XiaLize/MineFetch/issues) 反馈问题
- 反馈时请附上：
  - 运行的完整命令
  - 控制台错误信息
  - Windows 版本
  - 下载的 Minecraft 版本

## 开发者

构建方法、代码结构、BMCLAPI 映射和本地测试说明见 [DEVELOPING.md](DEVELOPING.md)。

源码仓库：<https://github.com/XiaLize/MineFetch>

## 致谢

- [BMCLAPI](https://bmclapi2.bangbang93.com/)：默认下载镜像
- [BMCLAPI 文档](https://bmclapidoc.bangbang93.com/)
- [Minecraft Wiki：教程/编写启动器](https://zh.minecraft.wiki/w/Tutorial:%E7%BC%96%E5%86%99%E5%90%AF%E5%8A%A8%E5%99%A8)

Minecraft 是 Mojang Synergies AB 的商标；本项目与 Mojang / Microsoft 无隶属关系。

## 许可

[MIT](LICENSE)

（使用AI工具编写）
