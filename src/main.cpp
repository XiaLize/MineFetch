#include "common.h"
#include "folder_dialog.h"
#include "http.h"
#include "installer.h"
#include "json.h"
#include "minecraft.h"
#include "mirror.h"
#include "remote.h"
#include "sha1.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kVersion = "1.0.0";
std::atomic_bool g_cancel{false};

BOOL WINAPI consoleControlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_cancel.store(true);
        return TRUE;
    }
    return FALSE;
}

std::string join(const std::vector<std::string>& values, size_t begin) {
    std::string result;
    for (size_t i = begin; i < values.size(); ++i) {
        if (!result.empty()) {
            result += ' ';
        }
        result += values[i];
    }
    return result;
}

void printBanner() {
    mf::Console::println("");
    mf::Console::println("  MineFetch " + std::string(kVersion) +
                         "  -  Minecraft Java 下载器");
    mf::Console::println("  C++ / WinHTTP / BMCLAPI  |  Windows x64");
    mf::Console::println("");
}

void printHelp() {
    printBanner();
    mf::Console::println("用法:");
    mf::Console::println("  minefetch install <版本> [选项]     下载并校验一个版本");
    mf::Console::println("  minefetch <版本> [选项]              install 的简写");
    mf::Console::println("  minefetch list [选项]               列出可下载版本");
    mf::Console::println("  minefetch info <版本> [选项]        查看版本信息");
    mf::Console::println("  minefetch                           进入交互模式");
    mf::Console::println("");
    mf::Console::println("选项:");
    mf::Console::println("  -d, --dir <目录>        游戏目录（默认 %APPDATA%\\.minecraft）");
    mf::Console::println("      --pick-dir          弹窗选择 .minecraft 文件夹");
    mf::Console::println("      --no-pick           不自动弹窗，直接使用默认/指定目录");
    mf::Console::println("  -m, --mirror <源>       下载源：bmclapi（默认）或 mojang");
    mf::Console::println("      --mirror-url <URL>  BMCLAPI 兼容镜像的根地址");
    mf::Console::println("  -t, --threads <1-32>    并发下载线程数（默认 8）");
    mf::Console::println("  -f, --force             忽略本地文件，全部重新下载");
    mf::Console::println("      --no-assets         不下载资源文件（assets）");
    mf::Console::println("      --no-logging        不下载日志配置");
    mf::Console::println("      --no-verify         不校验 SHA-1（速度更快但不安全）");
    mf::Console::println("      --no-fallback       禁止 BMCLAPI 失败后自动切换官方源");
    mf::Console::println("      --retries <0-10>    失败重试次数（默认 3）");
    mf::Console::println("      --timeout <秒>      网络超时（默认 30）");
    mf::Console::println("  -v, --verbose           显示正在下载的文件");
    mf::Console::println("      --type <类型>       list 过滤：release/snapshot/all（默认 release）");
    mf::Console::println("      --limit <数量>      list 显示数量，0 表示全部（默认 30）");
    mf::Console::println("  -h, --help              显示帮助");
    mf::Console::println("      --version           显示版本");
    mf::Console::println("");
    mf::Console::println("示例:");
    mf::Console::println("  minefetch install 1.20.1");
    mf::Console::println("  minefetch install latest --dir D:\\Games\\.minecraft -t 12");
    mf::Console::println("  minefetch list --type all --limit 0");
    mf::Console::println("  minefetch info 1.21.4 --mirror mojang");
    mf::Console::println("");
    mf::Console::println("说明:");
    mf::Console::println("  * 默认通过 BMCLAPI 下载。BMCLAPI 文件版权归原站点所有。");
    mf::Console::println("  * 未指定 --dir 时，交互运行会弹窗选择目录；--no-pick 可关闭。");
    mf::Console::println("  * BMCLAPI 不可用时会自动回退官方源，可用 --no-fallback 关闭。");
    mf::Console::println("  * 本程序只下载原版游戏文件，不包含登录、启动和 natives 解压。");
    mf::Console::println("  * 下载后会按官方 SHA-1 校验，失败文件可用同一命令重试。");
    mf::Console::println("");
}

struct Options {
    std::string command;
    std::string version;
    std::string gameDir;
    std::string mirror = "bmclapi";
    std::string mirrorUrl;
    int threads = 8;
    int retries = 3;
    int timeoutSeconds = 30;
    int limit = 30;
    std::string listType = "release";
    bool force = false;
    bool noAssets = false;
    bool noLogging = false;
    bool verify = true;
    bool verbose = false;
    bool fallbackOfficial = true;
    bool pickDir = false;
    bool noPick = false;
    bool help = false;
    bool versionFlag = false;
    bool selfTest = false;
};

int parseInteger(const std::string& value, const std::string& option) {
    if (value.empty()) {
        throw std::runtime_error(option + " 需要一个整数");
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        throw std::runtime_error(option + " 的值不是整数: " + value);
    }
    return static_cast<int>(parsed);
}

std::string optionValue(const std::vector<std::string>& args, size_t& index,
                        const std::string& option) {
    if (index + 1 >= args.size()) {
        throw std::runtime_error(option + " 缺少参数");
    }
    return args[++index];
}

std::string valueAfterEquals(const std::string& arg, const std::string& prefix) {
    return arg.substr(prefix.size());
}

Options parseOptions(const std::vector<std::string>& args) {
    Options options;
    std::vector<std::string> positional;
    bool endOptions = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (!endOptions && arg == "--") {
            endOptions = true;
        } else if (!endOptions && (arg == "-h" || arg == "--help")) {
            options.help = true;
        } else if (!endOptions && arg == "--version") {
            options.versionFlag = true;
        } else if (!endOptions && arg == "--self-test") {
            options.selfTest = true;
        } else if (!endOptions && (arg == "-d" || arg == "--dir")) {
            options.gameDir = optionValue(args, i, arg);
        } else if (!endOptions && mf::startsWith(arg, "--dir=")) {
            options.gameDir = valueAfterEquals(arg, "--dir=");
        } else if (!endOptions && (arg == "-m" || arg == "--mirror")) {
            options.mirror = optionValue(args, i, arg);
        } else if (!endOptions && mf::startsWith(arg, "--mirror=")) {
            options.mirror = valueAfterEquals(arg, "--mirror=");
        } else if (!endOptions && arg == "--mirror-url") {
            options.mirrorUrl = optionValue(args, i, arg);
        } else if (!endOptions && mf::startsWith(arg, "--mirror-url=")) {
            options.mirrorUrl = valueAfterEquals(arg, "--mirror-url=");
        } else if (!endOptions && (arg == "-t" || arg == "--threads")) {
            options.threads = parseInteger(optionValue(args, i, arg), arg);
        } else if (!endOptions && mf::startsWith(arg, "--threads=")) {
            options.threads = parseInteger(valueAfterEquals(arg, "--threads="), "--threads");
        } else if (!endOptions && arg == "--retries") {
            options.retries = parseInteger(optionValue(args, i, arg), arg);
        } else if (!endOptions && mf::startsWith(arg, "--retries=")) {
            options.retries = parseInteger(valueAfterEquals(arg, "--retries="), "--retries");
        } else if (!endOptions && arg == "--timeout") {
            options.timeoutSeconds = parseInteger(optionValue(args, i, arg), arg);
        } else if (!endOptions && mf::startsWith(arg, "--timeout=")) {
            options.timeoutSeconds = parseInteger(valueAfterEquals(arg, "--timeout="), "--timeout");
        } else if (!endOptions && arg == "--limit") {
            options.limit = parseInteger(optionValue(args, i, arg), arg);
        } else if (!endOptions && mf::startsWith(arg, "--limit=")) {
            options.limit = parseInteger(valueAfterEquals(arg, "--limit="), "--limit");
        } else if (!endOptions && arg == "--type") {
            options.listType = optionValue(args, i, arg);
        } else if (!endOptions && mf::startsWith(arg, "--type=")) {
            options.listType = valueAfterEquals(arg, "--type=");
        } else if (!endOptions && (arg == "-f" || arg == "--force")) {
            options.force = true;
        } else if (!endOptions && arg == "--no-assets") {
            options.noAssets = true;
        } else if (!endOptions && arg == "--no-logging") {
            options.noLogging = true;
        } else if (!endOptions && arg == "--no-verify") {
            options.verify = false;
        } else if (!endOptions && arg == "--no-fallback") {
            options.fallbackOfficial = false;
        } else if (!endOptions && (arg == "--pick-dir" || arg == "--pick")) {
            options.pickDir = true;
        } else if (!endOptions && arg == "--no-pick") {
            options.noPick = true;
        } else if (!endOptions && (arg == "-v" || arg == "--verbose")) {
            options.verbose = true;
        } else if (!endOptions && !arg.empty() && arg[0] == '-') {
            throw std::runtime_error("未知选项: " + arg);
        } else {
            positional.push_back(arg);
        }
    }

    if (!positional.empty()) {
        options.command = mf::toLowerAscii(positional[0]);
        if (options.command == "install" || options.command == "download") {
            options.command = "install";
            if (positional.size() >= 2) {
                options.version = join(positional, 1);
            }
        } else if (options.command == "list" || options.command == "versions") {
            options.command = "list";
        } else if (options.command == "info") {
            if (positional.size() >= 2) {
                options.version = join(positional, 1);
            }
        } else if (options.command == "help") {
            options.help = true;
        } else {
            options.command = "install";
            options.version = join(positional, 0);
        }
    }

    if (options.threads < 1 || options.threads > 32) {
        throw std::runtime_error("--threads 必须在 1 到 32 之间");
    }
    if (options.retries < 0 || options.retries > 10) {
        throw std::runtime_error("--retries 必须在 0 到 10 之间");
    }
    if (options.timeoutSeconds < 5 || options.timeoutSeconds > 300) {
        throw std::runtime_error("--timeout 必须在 5 到 300 秒之间");
    }
    if (options.limit < 0) {
        throw std::runtime_error("--limit 不能为负数");
    }
    if (options.command == "info" && options.version.empty() && !options.help &&
        !options.versionFlag && !options.selfTest) {
        throw std::runtime_error("info 需要指定版本号");
    }
    return options;
}

mf::fs::path resolveGameDirectory(const std::string& configured) {
    if (configured.empty()) {
        return mf::defaultGameDirectory();
    }
    std::error_code ec;
    mf::fs::path path = mf::pathFromUtf8(configured);
    if (path.is_relative()) {
        path = mf::fs::absolute(path, ec);
        if (ec) {
            throw std::runtime_error("无法解析游戏目录: " + ec.message());
        }
    }
    return path;
}

mf::fs::path chooseGameDirectory(const Options& options, bool automatic) {
    const mf::fs::path fallback = resolveGameDirectory(options.gameDir);
    const bool shouldPick =
        options.pickDir ||
        (automatic && options.gameDir.empty() && !options.noPick && mf::Console::isTerminal());
    if (!shouldPick) {
        return fallback;
    }

    mf::fs::path initial = fallback;
    std::error_code ec;
    if (!mf::fs::is_directory(initial, ec) && !initial.parent_path().empty()) {
        initial = initial.parent_path();
    }
    mf::Console::println("请选择 .minecraft 文件夹（可以新建文件夹）。");
    mf::fs::path selected;
    bool cancelled = false;
    std::string error;
    if (mf::chooseFolder(initial, L"选择 .minecraft 文件夹", selected, cancelled, error)) {
        mf::Console::println("已选择游戏目录: " + mf::pathToUtf8(selected));
        return selected;
    }
    if (cancelled) {
        mf::Console::println("已取消选择，使用目录: " + mf::pathToUtf8(fallback));
    } else {
        mf::Console::warning("文件夹选择失败: " + error);
        mf::Console::println("使用默认目录: " + mf::pathToUtf8(fallback));
    }
    return fallback;
}

mf::Mirror makeConfiguredMirror(const Options& options) {
    if (!options.mirrorUrl.empty()) {
        return mf::makeMirror("", options.mirrorUrl);
    }
    return mf::makeMirror(options.mirror);
}

mf::HttpOptions makeHttpOptions(const Options& options) {
    mf::HttpOptions http;
    http.timeoutSeconds = options.timeoutSeconds;
    http.maxAttempts = std::clamp(options.retries + 1, 1, 12);
    return http;
}

std::string shortDate(const std::string& value) {
    return value.size() >= 10 ? value.substr(0, 10) : value;
}

bool isBmclapiMirror(const mf::Mirror& mirror) {
    return mirror.root.find("bangbang93") != std::string::npos;
}

struct ManifestResult {
    mf::Manifest manifest;
    mf::Mirror mirror;
};

ManifestResult fetchManifestForDisplay(const Options& options, mf::Mirror mirror) {
    mf::Console::println("正在获取版本清单（" + mirror.name + "）...");
    mf::HttpOptions httpOptions = makeHttpOptions(options);
    if (isBmclapiMirror(mirror) && options.fallbackOfficial) {
        // Do not sit on a 30-second connect timeout or a 45-second rate-limit
        // wait just to show a version list.
        httpOptions.maxAttempts = 1;
        httpOptions.timeoutSeconds = std::min(httpOptions.timeoutSeconds, 8);
    }
    try {
        mf::HttpClient http(httpOptions);
        mf::Manifest manifest = mf::fetchManifest(http, mirror, &g_cancel);
        return ManifestResult{std::move(manifest), std::move(mirror)};
    } catch (const mf::Cancelled&) {
        throw;
    } catch (const std::exception& error) {
        if (!isBmclapiMirror(mirror) || !options.fallbackOfficial) {
            throw;
        }
        mf::Console::warning("BMCLAPI 获取版本清单失败: " + std::string(error.what()));
        mf::Console::println("正在切换到 Mojang 官方源...");
        mf::Mirror official = mf::makeMirror("mojang");
        mf::HttpClient fallbackHttp(makeHttpOptions(options));
        mf::Manifest manifest = mf::fetchManifest(fallbackHttp, official, &g_cancel);
        return ManifestResult{std::move(manifest), std::move(official)};
    }
}

int runList(const Options& options, const mf::Mirror& mirror) {
    const ManifestResult result = fetchManifestForDisplay(options, mirror);
    const mf::Manifest& manifest = result.manifest;
    const mf::Mirror& activeMirror = result.mirror;
    const std::string type = mf::toLowerAscii(options.listType);
    mf::Console::println("下载源: " + activeMirror.name + "  (" + activeMirror.root + ")");
    mf::Console::println("最新正式版: " + manifest.latestRelease + "    最新快照: " +
                         manifest.latestSnapshot);
    mf::Console::println("");
    mf::Console::println("类型        版本                       发布日期");
    mf::Console::println("--------------------------------------------------");
    uint32_t shown = 0;
    for (const mf::VersionSummary& version : manifest.versions) {
        if (type != "all" && type != "*" && mf::toLowerAscii(version.type) != type) {
            continue;
        }
        if (options.limit > 0 && shown >= static_cast<uint32_t>(options.limit)) {
            break;
        }
        std::cout << std::left << std::setw(12) << version.type << std::setw(27)
                  << version.id << shortDate(version.releaseTime) << '\n';
        ++shown;
    }
    if (shown == 0) {
        mf::Console::println("没有匹配的版本。");
    } else {
        mf::Console::println("");
        mf::Console::println("共显示 " + std::to_string(shown) + " 个版本。");
    }
    return 0;
}

int runInfo(const Options& options, const mf::Mirror& mirror) {
    const ManifestResult result = fetchManifestForDisplay(options, mirror);
    const mf::Manifest& manifest = result.manifest;
    const mf::Mirror& activeMirror = result.mirror;
    const mf::VersionSummary summary = mf::resolveVersionSummary(manifest, options.version);
    const mf::fs::path gameDirectory = resolveGameDirectory(options.gameDir);
    const mf::fs::path versionDirectory =
        gameDirectory / L"versions" / mf::pathFromUtf8(summary.id);
    const mf::fs::path localJson =
        versionDirectory / (mf::pathFromUtf8(summary.id).wstring() + L".json");
    mf::HttpClient http(makeHttpOptions(options));
    const mf::VersionMeta version = mf::fetchVersionMeta(
        http, activeMirror, summary, localJson, options.verify, &g_cancel);

    mf::Console::println("下载源: " + activeMirror.name + "  (" + activeMirror.root + ")");
    mf::Console::println("版本: " + version.id);
    mf::Console::println("类型: " + (version.type.empty() ? "unknown" : version.type));
    mf::Console::println("发布时间: " + version.releaseTime);
    if (version.javaMajor > 0) {
        mf::Console::println("Java: " + std::to_string(version.javaMajor) +
                             (version.javaComponent.empty() ? "" : " (" + version.javaComponent + ")"));
    }
    if (version.hasClient) {
        mf::Console::println("客户端: " +
                             (version.client.hasSize ? mf::formatBytes(version.client.size)
                                                     : std::string("大小未知")));
        if (!version.client.sha1.empty()) {
            mf::Console::println("客户端 SHA-1: " + version.client.sha1);
        }
    }
    mf::Console::println("依赖库: " + std::to_string(version.libraries.size()) + " 个，当前平台需要 " +
                         std::to_string(mf::effectiveLibraryDownloads(version).size()) + " 个文件");
    if (version.hasAssetIndex) {
        mf::Console::println("资源索引: " + version.assetIndexId +
                             (version.assetIndexTotalSize > 0
                                  ? "，约 " + mf::formatBytes(version.assetIndexTotalSize)
                                  : ""));
    }
    if (version.hasLogging) {
        mf::Console::println("日志配置: " + version.loggingId);
    }
    if (!version.mainClass.empty()) {
        mf::Console::println("主类: " + version.mainClass);
    }
    if (!version.inheritsFrom.empty()) {
        mf::Console::println("继承自: " + version.inheritsFrom + "（本下载器不支持）");
    }
    return 0;
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error("自检失败: " + message);
    }
}

int runSelfTest() {
    const mf::json::Value value = mf::json::parse(
        R"({"text":"Minecraft \u4e16\u754c","number":42,"array":[true,null,-1.5],"nested":{"ok":false}})");
    require(value.getString("text") == "Minecraft 世界", "JSON Unicode");
    require(value.getInt64("number") == 42, "JSON number");
    require(value.find("array")->asArray().size() == 3, "JSON array");
    require(!value.find("nested")->getBool("ok", true), "JSON bool");

    require(mf::sha1Bytes("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1");

    const mf::Mirror bmcl = mf::makeMirror("bmclapi");
    require(bmcl.mapUrl("https://piston-meta.mojang.com/v1/packages/x/1.20.1.json") ==
                "https://bmclapi2.bangbang93.com/v1/packages/x/1.20.1.json",
            "镜像 URL 映射");
    require(bmcl.mapUrl("https://libraries.minecraft.net/com/mojang/x.jar") ==
                "https://bmclapi2.bangbang93.com/maven/com/mojang/x.jar",
            "依赖库 URL 映射");
    require(bmcl.assetUrl("abcdef") == "https://bmclapi2.bangbang93.com/assets/ab/abcdef",
            "资源 URL");
    require(mf::mavenPathFromName("com.mojang:brigadier:1.0.18") ==
                "com/mojang/brigadier/1.0.18/brigadier-1.0.18.jar",
            "Maven 路径");

    const mf::Manifest manifest = mf::parseManifest(
        R"({"latest":{"release":"1.20.1","snapshot":"23w31a"},"versions":[{"id":"1.20.1","type":"release","url":"https://example.invalid/1.20.1.json","sha1":"abc","releaseTime":"2023-06-12T00:00:00+00:00"},{"id":"23w31a","type":"snapshot","url":"https://example.invalid/23w31a.json","releaseTime":"2023-08-02T00:00:00+00:00"}]})");
    require(manifest.findVersion("1.20.1") != nullptr, "版本清单");
    require(mf::resolveVersionId(manifest, "latest") == "1.20.1", "latest 解析");
    require(mf::resolveVersionId(manifest, "23w31a") == "23w31a", "snapshot 解析");

    mf::Rule windowsRule;
    windowsRule.action = "allow";
    windowsRule.hasOs = true;
    windowsRule.osName = "windows";
    require(mf::ruleMatches(windowsRule), "Windows 规则");
    windowsRule.osName = "osx";
    require(!mf::ruleMatches(windowsRule), "非 Windows 规则");

    mf::Console::println("自检通过。");
    return 0;
}

int runInteractive(const Options& initialOptions);

}  // namespace

int wmain(int argc, wchar_t** argv) {
    mf::Console::initialize();
    SetConsoleCtrlHandler(consoleControlHandler, TRUE);

    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(std::max(0, argc - 1)));
    for (int i = 1; i < argc; ++i) {
        args.push_back(mf::wideToUtf8(argv[i]));
    }

    if (args.empty()) {
        if (mf::Console::isTerminal()) {
            return runInteractive(Options{});
        }
        printHelp();
        return 0;
    }

    try {
        Options options = parseOptions(args);
        if (options.help) {
            printHelp();
            return 0;
        }
        if (options.versionFlag) {
            mf::Console::println("MineFetch " + std::string(kVersion));
            return 0;
        }
        if (options.selfTest) {
            return runSelfTest();
        }
        const mf::Mirror mirror = makeConfiguredMirror(options);
        if (options.command.empty()) {
            if (mf::Console::isTerminal()) {
                return runInteractive(options);
            }
            printHelp();
            return 0;
        }
        if (options.command == "list") {
            return runList(options, mirror);
        }
        if (options.command == "info") {
            return runInfo(options, mirror);
        }

        mf::InstallOptions install;
        install.gameDirectory = chooseGameDirectory(options, true);
        install.mirror = mirror;
        install.threads = options.threads;
        install.retries = options.retries;
        install.timeoutSeconds = options.timeoutSeconds;
        install.force = options.force;
        install.noAssets = options.noAssets;
        install.noLogging = options.noLogging;
        install.verify = options.verify;
        install.verbose = options.verbose;
        install.fallbackOfficial = options.fallbackOfficial;
        mf::Installer installer(std::move(install), &g_cancel);
        const mf::InstallSummary summary = installer.run(options.version);
        if (summary.cancelled) {
            return 130;
        }
        return summary.okay ? 0 : 1;
    } catch (const mf::Cancelled&) {
        mf::Console::warning("操作已取消。");
        return 130;
    } catch (const mf::HttpError& error) {
        mf::Console::error(error.what());
        if (error.statusCode() == 429) {
            mf::Console::println("提示：BMCLAPI 已触发请求频率限制，请等待约 1 分钟后重试；"
                                 "也可以使用 --mirror mojang。");
        } else if (error.statusCode() == 403) {
            mf::Console::println("提示：服务器拒绝了请求（403），可能是防盗链或 WAF 限制。");
        } else {
            mf::Console::println("提示：如果 HTTPS 连接失败，请检查系统时间、代理和证书设置；"
                                 "也可以尝试 --mirror mojang。");
        }
        return 1;
    } catch (const std::exception& error) {
        mf::Console::error(error.what());
        return 1;
    }
}

namespace {

int runInteractive(const Options& initialOptions) {
    printBanner();
    Options baseOptions = initialOptions;
    const mf::Mirror mirror = makeConfiguredMirror(baseOptions);
    mf::Console::println("下载源: " + mirror.name + "  (" + mirror.root + ")");
    const mf::fs::path gameDirectory = chooseGameDirectory(baseOptions, true);
    mf::Console::println("");
    mf::Console::println("输入版本号，例如 1.20.1；直接回车下载最新正式版。");
    mf::Console::println("输入 list 查看版本，输入 q 退出。");
    mf::Console::println("");

    for (;;) {
        mf::Console::print("minefetch> ");
        std::string line;
        if (!std::getline(std::cin, line)) {
            return 0;
        }
        line = mf::trim(line);
        if (line.empty()) {
            line = "latest";
        }
        if (mf::iequals(line, "q") || mf::iequals(line, "quit") || mf::iequals(line, "exit")) {
            return 0;
        }
        try {
            Options options = baseOptions;
            if (mf::iequals(line, "list")) {
                options.command = "list";
                options.limit = 20;
                runList(options, mirror);
                mf::Console::println("");
                continue;
            }
            if (mf::iequals(line, "help") || line == "?") {
                printHelp();
                continue;
            }
            options.command = "install";
            options.version = line;
            mf::InstallOptions install;
            install.gameDirectory = gameDirectory;
            install.mirror = mirror;
            install.threads = options.threads;
            install.retries = options.retries;
            install.timeoutSeconds = options.timeoutSeconds;
            install.force = options.force;
            install.noAssets = options.noAssets;
            install.noLogging = options.noLogging;
            install.verify = options.verify;
            install.verbose = options.verbose;
            install.fallbackOfficial = options.fallbackOfficial;
            mf::Installer installer(std::move(install), &g_cancel);
            const mf::InstallSummary summary = installer.run(line);
            if (summary.cancelled) {
                return 130;
            }
            mf::Console::println("");
            mf::Console::println("按回车继续，输入 q 退出。");
            std::string next;
            if (!std::getline(std::cin, next) || mf::iequals(mf::trim(next), "q")) {
                return summary.okay ? 0 : 1;
            }
        } catch (const mf::Cancelled&) {
            mf::Console::warning("操作已取消。");
            return 130;
        } catch (const std::exception& error) {
            mf::Console::error(error.what());
            mf::Console::println("");
        }
    }
}

}  // namespace
