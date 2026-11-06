# 安装细节

## 支持的 OBS 版本

| 平台 | 支持的 OBS 版本 | 说明 |
| --- | --- | --- |
| Windows 10 / 11 x64 | OBS Studio **28.0 – 33.x** | 插件按 28–33 的共同 ABI 构建，并在 28.0 / 28.1 / 29.0 / 29.1 / 30.0 / 30.2 / 31.1 / 32.2 / 33.0 上做过载入验证 |
| macOS 12+ | OBS Studio 见 [README](../README.md) | 见上面的兼容性表 |
| Ubuntu 22.04 / 24.04 | 与构建时 PPA 的 OBS 相同及更新版本 | `.deb` 与系统 libobs 链接 |
| Flatpak | 与 `com.obsproject.Studio` stable runtime 一致 | flatpak 包只匹配该 runtime |

## Windows 安装程序做了什么

- 安装到 `C:\ProgramData\obs-studio\plugins\obs-kaibo\`（OBS 28 起的官方第三方插件目录，安装版和 Steam 版 OBS 都会加载）。
- 需要管理员权限（写入 ProgramData）。不写注册表以外的系统位置；卸载信息由 Windows 的「应用」列表管理。
- 安装或卸载前会检查 OBS 是否在运行，请先关闭 OBS。
- 升级：直接运行新版安装程序。安装程序的身份（AppId）固定，会原地升级并清掉旧版不再需要的文件，设置和登录保留。

## 手动安装（zip）

把压缩包里的 `obs-kaibo` 文件夹放到：

```text
C:\ProgramData\obs-studio\plugins\
└── obs-kaibo\
    ├── bin\64bit\obs-kaibo.dll
    └── data\ (locale、theme、icons)
```

## 便携版 OBS

便携模式下 OBS 从自身目录加载第三方插件。把文件放到：

```text
<OBS 目录>\obs-plugins\64bit\obs-kaibo.dll
<OBS 目录>\data\obs-plugins\obs-kaibo\   ← zip 里 data 文件夹的内容
```

（OBS 33 的便携版仍然从 `obs-plugins\64bit` 读取第三方插件；`core\` 目录只放 OBS 自带插件，不要把手动安装的 Kaibo 放进 `core`。）

便携模式下 Kaibo 的数据也保存在 OBS 目录内（`config\obs-studio\plugin_config\obs-kaibo\`），随 OBS 一起移动和备份。

## macOS

`.pkg` 安装到 `/Library/Application Support/obs-studio/plugins/obs-kaibo.plugin`。卸载时删除该 `.plugin` 即可；数据位置见 [privacy.md](privacy.md)。

## Linux

- `.deb`：`sudo apt install ./obs-kaibo-*.deb`，卸载 `sudo apt remove obs-kaibo`。
- Flatpak：`flatpak install --user ./obs-kaibo-*-flatpak-x86_64.flatpak`；升级加 `--or-update`；卸载 `flatpak uninstall --user com.obsproject.Studio.Plugin.Kaibo`。
