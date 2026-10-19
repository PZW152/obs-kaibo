<p align="center"><img src="docs/images/kaibo-lockup.png" width="300" alt="Kaibo 开播"></p>

<p align="center">
  <a href="https://github.com/PZW152/obs-kaibo/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/PZW152/obs-kaibo?color=d92d55&label=release"></a>
  <a href="https://github.com/PZW152/obs-kaibo/actions/workflows/push.yaml"><img alt="Build" src="https://img.shields.io/github/actions/workflow/status/PZW152/obs-kaibo/push.yaml?branch=main&label=build"></a>
  <a href="https://github.com/PZW152/obs-kaibo/actions/workflows/tests.yaml"><img alt="Tests" src="https://img.shields.io/github/actions/workflow/status/PZW152/obs-kaibo/tests.yaml?branch=main&label=tests"></a>
  <a href="https://github.com/PZW152/obs-kaibo/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/PZW152/obs-kaibo/total?color=111214"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/github/license/PZW152/obs-kaibo?color=111214"></a>
  <img alt="OBS 30+" src="https://img.shields.io/badge/OBS-30%2B-111214">
</p>

<p align="center"><b>在 OBS 里一键开播哔哩哔哩。</b><br>扫码登录 · 改标题和分区 · 自动写入推流设置 · 断流提醒 · 简体 / 繁體 / English</p>

<p align="center">
  <a href="https://github.com/PZW152/obs-kaibo/releases/latest">下载</a> ·
  <a href="README.zh-TW.md">繁體中文</a> ·
  <a href="README.en.md">English</a> ·
  <a href="docs/troubleshooting.md">常见问题</a>
</p>

<p align="center"><img src="docs/images/dock-design.png" width="640" alt="Kaibo 面板设计稿：深色与浅色主题"></p>
<p align="center"><sub>面板设计稿（深色 / 浅色主题，跟随 OBS 自动切换）</sub></p>

## 它做什么

Kaibo 是 OBS Studio 的一个停靠面板（Dock）。登录一次之后，开播只需要按一个按钮：

1. 把你改过的**标题、分区、直播间公告**同步到 B 站；
2. 开启直播间，拿到推流地址和推流码；
3. **自动写入 OBS 的推流设置**，并（默认）直接开始推流。

结束直播同样是一个按钮：停止 OBS 推流并关闭直播间，告诉你这场播了多久。

## 功能

| | |
| --- | --- |
| 扫码登录 | 面板内直接显示二维码，显示「已扫码 / 已过期」状态，过期一键刷新 |
| 直播间信息 | 标题（40 字计数）、最近用过的标题、分区搜索与「最近使用」、直播间公告 |
| 一键开播 / 关播 | 未同步的修改在开播时自动同步；可选开播后自动推流 |
| 状态对账 | 区分「直播中」「直播间开着但 OBS 没推流」「推流断了」「OBS 在推流但没开播」，每种情况给出唯一的修复按钮 |
| 断流与重连 | 网络断线不会关闭直播间，一键重新推流；OBS 重连中会显示 |
| 关闭 OBS 时 | 可选自动关闭直播间，避免「挂空播」 |
| 多平台直播 | 可选关播后恢复你原来的 OBS 推流设置 |
| 人脸认证 | B 站要求实名认证时，直接在 OBS 里显示认证二维码，完成后继续开播 |
| 快捷键 | 「Kaibo：一键开播」「Kaibo：结束直播」可在 OBS 快捷键设置里绑定 |
| 主题 | 跟随 OBS 深色 / 浅色主题；所有文字对比度符合 WCAG AA |
| 语言 | 简体中文、繁體中文、English，跟随 OBS 语言 |
| 隐私 | 登录凭证在 Windows 上用 DPAPI 加密，只存在本机一个文件夹；可一键删除全部数据 |
| 旧插件迁移 | 检测到旧版「BILIBILI Stream for OBS」的登录信息时可一键导入，免重新扫码 |

## 安装

需要 **OBS Studio 30 或更高版本**。

### Windows（推荐安装程序）

1. 从 [Releases](https://github.com/PZW152/obs-kaibo/releases/latest) 下载 `obs-kaibo-<版本>-windows-x64-installer.exe`。
2. 关闭 OBS，运行安装程序。插件会安装到 `C:\ProgramData\obs-studio\plugins\obs-kaibo`（OBS 官方插件目录，Steam 版 OBS 也适用）。
3. 打开 OBS，在 **停靠窗口（Docks）→ Kaibo 开播** 或 **工具 → Kaibo 开播** 打开面板。

升级时直接运行新版本安装程序即可，设置和登录都会保留。

**便携版 OBS / 不想用安装程序**：下载 `obs-kaibo-<版本>-windows-x64.zip`，把里面的 `obs-kaibo` 文件夹放进 `C:\ProgramData\obs-studio\plugins\`（便携版 OBS 放进 OBS 目录下的 `obs-plugins` 对应位置，见 [docs/install.md](docs/install.md)）。

### macOS

下载 `obs-kaibo-<版本>-macos-universal.pkg` 并安装，插件位于 `/Library/Application Support/obs-studio/plugins/`。

### Linux

- Ubuntu / Debian：`sudo apt install ./obs-kaibo-<版本>-x86_64-ubuntu-24.04.deb`
- Flatpak 版 OBS：`flatpak install --user ./obs-kaibo-<版本>-flatpak-x86_64.flatpak`

## 卸载与数据

- **Windows**：在「设置 → 应用」里卸载「Kaibo for OBS Studio」。卸载默认**保留**你的设置和登录，方便以后重装；卸载时会询问是否一并删除。
- 也可以在 Kaibo 面板的 **设置 → 删除所有 Kaibo 数据** 中随时清除。
- Kaibo 写入的全部文件、位置和用途见 [docs/privacy.md](docs/privacy.md)。

## 常见问题

见 [docs/troubleshooting.md](docs/troubleshooting.md)：二维码扫不了、开播报错、分区「需申请」、人脸认证、日志位置等。

## 开发

见 [docs/building.md](docs/building.md) 和 [docs/architecture.md](docs/architecture.md)。界面遵循 [Kaibo 设计系统](design/README.md)，颜色等数值来自 `data/theme/tokens.json`，网页设计稿和插件共用同一份。

## 致谢与许可

Kaibo 由 [Zarosmm/obs-bilibili-stream](https://github.com/Zarosmm/obs-bilibili-stream)（BILIBILI Stream for OBS）重构而来，感谢原作者 Zaros 和所有贡献者。以 [GPL-2.0](LICENSE) 发布；第三方组件见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

Kaibo 是独立的开源工具，与哔哩哔哩（上海宽娱数码科技有限公司）无关，也未获其认可。「哔哩哔哩」「bilibili」是其各自所有者的商标。
