# 架构

```text
src/
├── core/        纯 C++17，不依赖 OBS 和 Qt，有单元测试
│   ├── bili_client   B 站接口：登录、直播间、分区、开播/关播、公告、退出（通过 HttpTransport 注入网络层）
│   ├── cookies       会话 Cookie 解析与合并
│   ├── areas         分区列表解析、搜索、最近使用
│   ├── settings_model settings.json 的读写与默认值；原子写入
│   ├── text / md5    URL 编码、计数格式化（1.2万 / 1.2萬 / 12K）、签名用 MD5
│   └── http.hpp      网络抽象
├── app/         与 OBS 交互的逻辑层
│   ├── controller    LiveController：唯一的状态来源，所有业务流程（UI 线程）
│   ├── worker        单个后台线程跑所有网络请求，结果以事件投递回 UI 线程
│   ├── obs_bridge    改写/恢复 OBS 推流设置、开始/停止推流、监听推流断开与重连
│   ├── store         数据文件的唯一出入口（settings.json、credentials.bin、旧插件导入）
│   ├── secret_store  凭证加密（Windows DPAPI）
│   └── curl_transport libcurl 实现，强制 HTTPS、不跟随重定向
├── ui/          Qt 界面，只读 controller 的状态、只发意图
│   ├── dock          面板：登录页 / 主页 / 设置页
│   ├── widgets       状态胶囊、头像、二维码、开关、横幅、Toast
│   ├── area_picker   分区搜索弹层
│   └── theme         读取 data/theme/tokens.json，生成样式表与着色图标
├── vendor/      json11、qrcodegen（MIT）
└── plugin-main.cpp  模块入口：注册 Dock、工具菜单、快捷键、前端事件
```

## 关键决策

- **单线程网络**：`BiliClient` 不是线程安全的，所有调用都在 `worker` 的一个线程上排队执行；UI 线程永远不会等网络。回调用 `QPointer` 守护，界面销毁后自动丢弃。
- **状态对账**：直播间状态（B 站）和推流状态（OBS）分开记录，`LiveController::phase()` 合成为 6 种情况，界面只根据它渲染文案和修复按钮。
- **断流与主动停止的区分**：订阅推流输出的 `stop` 信号，`code == OBS_OUTPUT_SUCCESS` 才算主动停止；其余视为断流，不会自动关闭直播间。
- **设计系统即数据**：`data/theme/tokens.json` 与在线设计系统共用；`kaibo.qss` 用 `@token@` 占位，运行时按深色/浅色替换。改颜色只改一处。
- **数据所有权**：Kaibo 只写 `plugin_config/obs-kaibo/` 下列出的文件，删除时按清单删除，不扫描、不猜测。
- **人脸认证是状态而不是弹窗**：B 站要求人脸认证（60024/60043）时进入 `FaceAuth` 状态，由主页面内的卡片驱动；扫码完成后自动按退避重试开播，避免重复扫码和嵌套弹窗。
- **向后兼容**：`settings.json` 带 `schema` 字段；读取时忽略未知字段、缺失字段取默认值，损坏时回退到 `.bak`。

## 线程模型

| 线程 | 做什么 |
| --- | --- |
| OBS UI 线程 | 全部 Qt 界面、`LiveController`、OBS 前端 API 调用 |
| Kaibo worker | 所有 HTTP 请求（libcurl） |
| OBS 输出 / 快捷键线程 | 只把事件投递回 UI 线程，不做任何处理 |
