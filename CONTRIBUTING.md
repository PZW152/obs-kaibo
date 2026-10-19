# 参与贡献 / Contributing

欢迎 Issue 和 Pull Request。/ Issues and pull requests are welcome.

- **先读** [docs/architecture.md](docs/architecture.md)：网络只在 worker 线程，界面只读 `LiveController` 的状态。
- **构建与测试**：[docs/building.md](docs/building.md)。改了 `src/core` 请补单元测试；改了文案请同时更新 `data/locale/` 的三种语言并运行 `python3 tools/check-locales.py`。
- **界面**：颜色、间距、圆角只能来自 `data/theme/tokens.json`；不要在代码里写颜色值。
- **格式**：clang-format 19（`build-aux/run-clang-format`）与 gersemi（`build-aux/run-gersemi`）。
- **隐私**：不要在日志、测试数据或 Issue 中出现真实 Cookie 或推流码。
- 提交即表示你同意以 GPL-2.0 发布你的贡献。/ Contributions are licensed under GPL-2.0.
