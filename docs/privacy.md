# 数据、隐私与卸载

Kaibo 只在一个文件夹里保存自己的数据，不写注册表、不装服务、不加开机启动项，也不收集任何统计信息。

## Kaibo 写入的文件

全部位于 OBS 为插件分配的配置目录 `plugin_config/obs-kaibo/`：

| 系统 | 位置 |
| --- | --- |
| Windows | `%APPDATA%\obs-studio\plugin_config\obs-kaibo\` |
| macOS | `~/Library/Application Support/obs-studio/plugin_config/obs-kaibo/` |
| Linux | `~/.config/obs-studio/plugin_config/obs-kaibo/` |
| Flatpak | `~/.var/app/com.obsproject.Studio/config/obs-studio/plugin_config/obs-kaibo/` |
| 便携版 OBS | `<OBS 目录>\config\obs-studio\plugin_config\obs-kaibo\` |

| 文件 | 内容 |
| --- | --- |
| `settings.json`（及 `.bak`） | 选项、最近使用的分区和标题、关播后要恢复的推流设置备份 |
| `credentials.bin`（及 `.bak`） | B 站登录 Cookie（SESSDATA、bili_jct 等） |

- **Windows**：`credentials.bin` 用 Windows DPAPI 加密，只有同一台电脑上的同一个 Windows 账号能解密。
- **macOS / Linux**：以 `0600` 权限保存（只有你的系统账号可读），未额外加密。
- 写入采用「先写临时文件再替换」，并保留上一版 `.bak`，断电或崩溃不会留下损坏的文件。
- 推流码只保存在内存里，Kaibo 不会把它写进自己的文件；日志中不会出现 Cookie 或推流码。

## Kaibo 修改的 OBS 设置

开播时，Kaibo 会把当前 OBS 配置文件（Profile）的**推流设置**改为「自定义」并填入 B 站给的服务器和推流码——这和你手动粘贴是一样的。开启「关播后恢复原来的推流设置」后，Kaibo 会在第一次改写前备份原设置，关播后改回去。

Kaibo 会把两个快捷键（「一键开播」「结束直播」）的绑定保存在 OBS 场景集合里，这由 OBS 负责管理。

## 网络请求

Kaibo 只连接哔哩哔哩的 HTTPS 接口：

- `passport.bilibili.com`、`passport.biligame.com`：扫码登录、退出登录
- `api.bilibili.com`：读取账号昵称与头像地址
- `api.live.bilibili.com`：直播间信息、分区列表、修改标题/分区/公告、开播、关播
- `i0.hdslb.com` 等 B 站图片域名：下载你自己的头像

不连接任何其他服务器，没有更新检查、统计或遥测。

## 退出登录与删除数据

- **设置 → 退出登录**：通知 B 站让这次登录失效，并删除 `credentials.bin`。
- **设置 → 删除所有 Kaibo 数据**：退出登录并删除上表中的全部文件；文件夹为空时一并删除。只删除 Kaibo 自己的文件。
- **Windows 卸载程序**：删除 `C:\ProgramData\obs-studio\plugins\obs-kaibo\`。默认保留你的设置和登录；卸载时会询问是否删除上面的数据文件夹（只删除 `obs-kaibo` 这一个文件夹）。静默卸载（`/SILENT`）始终保留数据。
- **macOS / Linux**：删除插件后，如需彻底清除，请删除上表对应的 `obs-kaibo` 文件夹。

## 从旧插件迁移

如果你用过「BILIBILI Stream for OBS」（`bilibili-stream-for-obs`），Kaibo 会**只读**它的 `plugin_config/bilibili-stream-for-obs/config.json`，询问你是否导入登录信息。Kaibo 不会修改或删除旧插件的任何文件；Windows 安装程序提供一个默认不勾选的选项，可以删除旧插件的程序文件夹（只有确认里面是旧插件的 DLL 时才会显示）。
