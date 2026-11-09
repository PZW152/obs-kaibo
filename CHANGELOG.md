# Changelog

## Unreleased

### Fixed

- **人脸认证不再把人带到一个打不开的认证页**：此前 B 站没有在开播响应里返回认证链接时（60043 一直如此，60024 也可能），Kaibo 会自己拼一个 `face-auth-middle.html?source_event=400&mid=…` 地址。那个地址缺少 B 站签发的一次性参数，页面加载就失败，表现为「系统错误」或 `Cannot read properties of undefined (reading 'code')`；而**每试一次仍会占用账号当天的人脸认证提交次数**，试几次就会提示「单日提交验证达到上限」。现在认证链接只取 B 站真正返回的那个：拿不到就不显示二维码，也不显示「在浏览器中打开」，改为直接说明在手机「哔哩哔哩」App 或官方直播姬里完成认证，然后点「已完成认证，重试开播」。
  The face-verification link is now only ever the one Bilibili returned. Kaibo used to assemble a verification page URL when Bilibili returned none (60043 always, 60024 sometimes); that page cannot start a verification — it fails with a script error or "system error" — and each attempt still counted against the account's daily verification quota. With no link, the dock now says where to verify instead of showing a code that cannot work.
- 认证链接解析更宽：`qr` / `qrcode` / `qrcode_url` / `qr_url` / `face_url` / `face_auth_url` / `jump_url` / `url`，以及嵌套一层的同名字段；`//host/…` 与 `http://` 统一升到 https，`bilibili://` 深链保留（可扫但不在浏览器打开），其他形式（相对路径、裸 token、非 http(s) 协议）一律当作「没有链接」。
- 面板会显示 B 站对这次认证的原话（例如「目标分区需要人脸认证」），不再只有插件自己的措辞。
- 重试全部失败后的提示改为说明 B 站的单日提交上限，并建议先在手机 App 完成认证，而不是建议继续重试。

## 1.2.0 — 2026-11-06

### Compatibility

- **Windows 现在支持 OBS Studio 28 及以上**：此前最低是 29.1。去掉了只有 Qt 6.4+ 才有的调用后，插件能在 OBS 28.0 到 33.0 上载入。
  Windows now supports OBS Studio 28 and newer (previously 29.1).
- CI 现在对每次构建下载 28.0 到 33.0 的官方 OBS，核对符号并实际启动，防止以后悄悄不再兼容旧版。

### Fixed

- **登录失效能提前发现**：每 30 分钟用需要登录的接口确认一次会话。此前轮询只调用公开接口，登录过期后要到点开播或关播时才知道。
  An expired sign-in is now noticed within 30 minutes; the room poll only used a public endpoint, so it surfaced when you tried to go live or end.
- 开播时一起保存的公告不再一直显示「未保存」，下次开播也不会重复提交。
- 保存时标题和分区成功、公告失败，现在只有公告保持未保存。
- 人脸验证完成后，「开始推流」的意图不再丢失：房间打开后会接着开始推流。
- 简体中文的「将面板放到右侧全高」菜单项不再显示成繁体字。

### Changed

- **面板底部常驻操作栏**：开播按钮、本场时长和在线人数固定在面板底部，不会被滚动带走。直播中整条变成品牌色，需要处理时变成警告色，一眼就能看出状态。
  The go-live button, timer and viewer count now stay pinned at the bottom of the dock, and the bar's colour shows the broadcast's state.
- 账号和直播间信息合并成一张卡片；推流信息默认收起；状态文字缩短，窄面板不再被截断。
- 横幅按钮按实际文字宽度决定并排还是堆叠。
- **网络连接复用**：对同一主机的连续请求不再重新做 TCP/TLS 握手，开播和扫码轮询更快。请求失败后会丢弃连接重新建立。
- **按需轮询**：未开播时面板可见每 60 秒、隐藏时每 5 分钟刷新一次，离线时逐步退避；直播中保持 30 秒。
- 状态判断抽到 `core/live_state.hpp` 并补上单元测试；CI 增加 ASan + UBSan。

## 1.1.0 — 2026-11-03

### Compatibility

- **Windows 现在支持 OBS Studio 29.1 及以上**：产物改以 OBS 30.0.2 + Qt 6.5.3 为构建基线，向前兼容到 OBS 33.x（1.0.3 实际只能在 31.1+ 载入，OBS 29/30 会直接「Module not loaded」）。
  Windows now supports OBS Studio 29.1 and newer: the Windows build is pinned to OBS 30.0.2 + Qt 6.5.3 and runs on OBS 29.1 – 33.x (1.0.3 could only load on 31.1+).
- **dock 注册不再静态链接**：OBS 28/29 用 `obs_frontend_add_dock`，30+ 用 `obs_frontend_add_custom_qdock`（回退 `add_dock_by_id`），全部在运行时解析。`obs_frontend_add_dock` 从 OBS 32 起已被移除，静态导入是此前跨版本载入失败的根因之一。
- macOS 继续以 OBS 31.1.1 + Qt 6.8 为基线（受 Xcode 版本限制），支持 OBS 31.1+。
- Windows 安装程序、便携版路径、数据目录与插件 id 均不变，升级不会影响 1.0.x 的设置与登录。

### Added

- **开播前检查**：开播前检查登录是否有效、直播间是否存在、分区（含「需申请」）、标题/公告长度、是否离线；能提前说清的问题会在开播前给出本地化原因，阻塞项阻止开播，警告只提示。
- **面板放到右侧全高**：新增「工具 → 将面板放到右侧全高」。OBS 33 起面板默认停靠在右侧；该菜单项会在需要时把面板回到右侧并开启 OBS 的「全高度停靠窗口」，解决「面板不能顶天 / 右侧不能占满整列」的问题。
- **OBS 33 全高度提示**：首次检测到 OBS 支持「全高度停靠窗口」但未开启时，面板内会出现一次性提示（可关闭，记住选择）。
- **首次安装自动显示面板**：新装用户启动 OBS 后 Kaibo 面板会自动显示，不再只存在于工具菜单里（全新安装的 OBS 首次启动时可能以浮动窗口出现，用「工具 → 将面板放到右侧全高」一键归位；之后会记住布局）。
- **诊断日志**：`[obs-kaibo]` 日志覆盖关键状态变化与失败请求（操作、接口、错误类别、B 站错误码/HTTP 状态、B 站原文），不含 Cookie、令牌、推流码或完整 URL。

### Fixed

- 退出 OBS 时能可靠关闭直播间（此前关闭请求可能被工作线程的取消标记误取消）。
- 推流连接中（STARTING–STARTED 窗口）点「结束直播」会真正停掉 OBS 输出，不再出现房间已关但 OBS 继续推流。
- 关闭 OBS、重启后状态恢复：开播前检查/推流目标改为实时读取 OBS，重启后「开始推流」一键可用；「开播后自动推流」关闭时也不会再出现按钮点两次才生效。
- 只对真正的 B 站推流目标执行结束/关播：OBS 正在推其他平台时，断流提示、结束直播、自动关播都不会误伤。
- 推流设置的备份先加密落盘、确认成功后才改写 OBS；关播恢复只会在当前推流目标仍是 B 站时执行；旧版明文备份迁移在密封失败时不再删除原文件。
- 响应解析健壮性：缺少 `code` 字段、缺少 `isLogin`、缺少 `live_status`、布尔型 code、超大/小数/NaN 数字都不再被误判为登出/关播/成功。
- 二维码登录与轮询的竞态（隐藏面板、二维码过期后仍会重启轮询），以及普通请求轮换的 Cookie 未持久化。
- 分区「需申请」不再基于过期缓存硬性阻止开播；B 站推流地址按主机名边界匹配，不再把相似域名当成 B 站。
- 面板性能：输入标题、后台刷新、面板隐藏时不再反复重建界面；图标/二维码/头像/主题位图都有缓存，计时器只在需要时运行。
- 面板界面/易用性（1.0.3 审查遗留项）：提示条不再遮挡内容、窄面板省略显示 + tooltip、横幅与按钮在 280px 下不溢出、键盘与读屏、错误/离线/公告不可用状态、设计 token 一致化、三语文案与「B 站/哔哩哔哩」命名统一。

### Changed

- 设计系统新增 `tools/check-theme.py`（CI 检查硬编码颜色与 QSS token），语言文件继续由 `tools/check-locales.py` 检查。
- OBS 33 的停靠布局说明与兼容矩阵写入 `docs/install.md`、`docs/troubleshooting.md`。
- 1.0.3 的「结束直播」会立刻关闭房间，现在会等 OBS 推流真正停止后再关，避免观众端还有延迟缓冲时直接断掉。

### Verified

- 单元测试：39 tests / 430 checks（Windows / macOS / Ubuntu CI 全绿），新增取消作用域、推流地址主机名匹配、响应解析边界、Cookie/设备标识、会话代次等回归。
- CI 构建与打包（Windows 安装程序 + zip、macOS pkg、Ubuntu deb、Flatpak）全部通过；格式检查、语言检查、设计 token 检查通过。
- Windows 实测载入矩阵：OBS 29.1.3 / 30.2.3 / 31.1.2 / 32.2.2 / 33.0.0 / 33.0.1 均显示 `[obs-kaibo] loaded` + `started`，并按版本分别使用 `obs_frontend_add_dock`（29）与 `obs_frontend_add_custom_qdock`（30+）。

## 1.0.3 — 2026-10-25
