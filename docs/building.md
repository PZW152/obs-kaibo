# 构建与测试

## 单元测试（不需要 OBS）

`src/core` 不依赖 OBS 和 Qt，可以单独编译测试：

```bash
cmake -S tests -B build-tests
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

测试覆盖：MD5 与 B 站签名、URL/表单编码、Cookie 合并与删除、扫码登录两种 Cookie 下发方式、错误分类（网络/HTTP/解析/接口/未登录）、开播成功与两种人脸认证、标题编码、直播间信息解析、分区解析与搜索、设置读写与损坏回退、原子写入。

语言文件检查（三种语言键一致、占位符一致、代码用到的键都存在）：

```bash
python3 tools/check-locales.py
```

## 构建插件

插件使用 [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) 的构建系统，依赖 OBS 31、Qt 6（Core、Widgets、Svg）、libcurl，会自动下载。

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

## CI 与发布

- `Unit Tests`：每次推送在 Windows、macOS、Ubuntu 上跑核心测试和语言文件检查。
- `Push` / `Pull Request`：格式检查（clang-format 19、gersemi）并构建 Windows、macOS、Ubuntu、Flatpak 包。
- 推送 `x.y.z` 格式的标签会生成草稿 Release，附带 Windows 安装程序和 zip、macOS pkg、Ubuntu deb、Flatpak 包与 SHA-256 校验值。

## 代码风格

提交前运行 `build-aux/run-clang-format` 和 `build-aux/run-gersemi`（需要 clang-format 19）。界面颜色、间距、圆角只能来自 `data/theme/tokens.json`。
