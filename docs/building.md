# 构建与测试

## 单元测试（不需要 OBS）

`src/core` 不依赖 OBS 和 Qt，可以单独编译测试：

```bash
cmake -S tests -B build-tests
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

测试覆盖：MD5 与 B 站签名、URL/表单编码、Cookie 合并与删除、扫码登录两种 Cookie 下发方式、错误分类（网络/HTTP/解析/接口/未登录，含缺少 `code` 的响应与 HTTP 412 风控响应）、开播成功与两种人脸认证（含 60024 的二维码字段回退）、开播请求的毫秒时间戳与客户端身份请求头、设备标识（buvid3/buvid4）获取与持久化、API 响应 Set-Cookie 的吸收、人脸认证重试节奏、标题编码、计数格式（万/亿/M/K 的进位边界）、时间解析（不存在的日期会被拒绝）、直播间信息解析、分区解析与搜索、设置读写与默认值、原子写入（macOS/Linux 上临时文件权限 0600）、凭证加密往返。

`settings.json` 损坏时回退到 `.bak` 的逻辑在 `src/app/store.cpp`，依赖 OBS 的模块路径，不在上面的单元测试内。

语言文件检查（三种语言键一致、占位符一致、代码用到的键都存在）：

```bash
python3 tools/check-locales.py
```

设计系统检查（QSS / 代码里不得出现 tokens.json 之外的颜色，`@token@` 必须存在）：

```bash
python3 tools/check-theme.py
```

## 构建插件

插件使用 [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) 的构建系统。插件链接 Qt 6（Core、Widgets、Svg）、libcurl、libobs 与 obs-frontend-api。

### 兼容性基线（1.1.0 起）

发布产物按「最旧的受支持 OBS」构建，靠 ABI 向前兼容运行在更新的 OBS 上：

| 平台 | 构建基线 | 说明 |
| --- | --- | --- |
| Windows x64 | OBS 30.0.2 + obs-deps/Qt6 2023-11-03（Qt 6.5.3） | OBS 30.x 本身要用 legacy CMake 框架构建（`OBS_CMAKE_VERSION=2.0.0`）；产物可载入 OBS 28.0–33.x |
| macOS universal | 见 `buildspec.json` 的平台覆盖项 | 受 Xcode 版本限制，macOS 基线高于 Windows |
| Ubuntu 22.04 / 24.04 | 发行版仓库 / PPA 的 libobs 与 Qt6 | 不读 buildspec 的 OBS 源码版本 |
| Flatpak | `com.obsproject.Studio` stable runtime | 与 runtime 内的 OBS 匹配 |

Dock 注册 API 在各版本之间不同（`obs_frontend_add_dock` 在 32 起被移除，`obs_frontend_add_dock_by_id` / `obs_frontend_add_custom_qdock` 在 30 起才有），因此 `src/app/obs_frontend_compat.cpp` 用运行时符号解析（Windows `GetProcAddress`、POSIX `dlsym`），绝不静态导入这些函数。

```bash
# Windows（Visual Studio 2022）
cmake --preset windows-x64
cmake --build --preset windows-x64

# macOS（Xcode 16）
cmake --preset macos
cmake --build --preset macos

# Ubuntu
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
```

## 面板截图

`tests/ui-preview` 用假的 OBS 和网络层渲染真实的面板，覆盖登录、未开播、直播中、断流、人脸认证、设置等状态。CI 的 `Dock screenshots` 任务会产出深色和浅色两套截图（artifact：`dock-screenshots`）。改界面时看这组图，不用打开 OBS，也不用登录账号。

```bash
cmake -S tests/ui-preview -B build-preview
cmake --build build-preview
KB_OUT=shots QT_QPA_PLATFORM=offscreen build-preview/kaibo-ui-preview
```

## OBS 版本兼容性检查

`tools/check-obs-compat.py` 下载官方 OBS 发布包，核对插件导入的每个符号是否都由该版本提供，再在便携模式下启动 OBS，从日志确认插件是否载入。CI 的 `OBS version compatibility` 任务对 Windows 构建产物运行它，结果写在任务摘要里。`--floor` 指定的版本及更新的版本缺少符号、插件载入失败或 OBS 随后崩溃时，检查失败。CI 的下限是 28.0.0。

构建用的 Qt 比 OBS 28 自带的新，所以不能用只有新版 Qt 才导出的函数：`QObject::setObjectName` 要改用 `setObjectId()`（`src/app/qt_compat.hpp`），也不要用 `QSettings`。这项检查会拦下这类改动。

## CI 与发布

- `Unit Tests`：每次推送在 Windows、macOS、Ubuntu 上跑核心测试和语言文件检查。
- `Push` / `Pull Request`：格式检查（clang-format 19、gersemi）并构建 Windows、macOS、Ubuntu、Flatpak 包。
- 推送 `x.y.z` 格式的标签会生成草稿 Release，附带 Windows 安装程序和 zip、macOS pkg、Ubuntu deb、Flatpak 包与 SHA-256 校验值。

## 代码风格

提交前运行 `build-aux/run-clang-format` 和 `build-aux/run-gersemi`（需要 clang-format 19）。界面颜色、间距、圆角只能来自 `data/theme/tokens.json`。
