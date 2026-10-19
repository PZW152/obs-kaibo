<p align="center"><img src="docs/images/kaibo-lockup.png" width="300" alt="Kaibo 開播"></p>

<p align="center"><b>在 OBS 裡一鍵開播嗶哩嗶哩。</b><br>掃碼登入 · 修改標題和分區 · 自動寫入串流設定 · 斷流提醒 · 简体 / 繁體 / English</p>

<p align="center"><a href="https://github.com/PZW152/obs-kaibo/releases/latest">下載</a> · <a href="README.md">简体中文</a> · <a href="README.en.md">English</a></p>

<p align="center"><img src="docs/images/dock-design.png" width="640" alt="Kaibo 面板設計稿"></p>

## 它做什麼

Kaibo 是 OBS Studio 的停駐面板（Dock）。登入一次之後，開播只要按一個按鈕：把修改過的標題、分區和公告同步到 B 站 → 開啟直播間 → 自動寫入 OBS 串流設定並開始串流。結束直播也是一個按鈕：停止串流、關閉直播間，並告訴你這場播了多久。

主要功能：面板內掃碼登入、分區搜尋與「最近使用」、標題歷史、直播間公告、斷流後一鍵重新串流、直播間與 OBS 狀態不一致時給出修復按鈕、關閉 OBS 時自動關播、關播後還原原本的串流設定、人臉認證 QR 碼、快速鍵、跟隨 OBS 深色/淺色主題、簡體中文/繁體中文/English 介面、登入憑證以 Windows DPAPI 加密。

## 安裝

需要 **OBS Studio 30 以上**。

- **Windows**：從 [Releases](https://github.com/PZW152/obs-kaibo/releases/latest) 下載 `obs-kaibo-<版本>-windows-x64-installer.exe`，關閉 OBS 後執行。外掛會安裝到 `C:\ProgramData\obs-studio\plugins\obs-kaibo`（Steam 版 OBS 也適用）。升級時直接執行新版安裝程式，設定與登入都會保留。
- **macOS**：安裝 `obs-kaibo-<版本>-macos-universal.pkg`。
- **Linux**：Ubuntu/Debian 使用 `.deb`；Flatpak 版 OBS 使用 `.flatpak`（`flatpak install --user ./obs-kaibo-*.flatpak`）。

開啟 OBS 後，在 **停駐視窗 → Kaibo 開播** 或 **工具 → Kaibo 開播** 打開面板。

## 解除安裝與資料

Windows 從「設定 → 應用程式」解除安裝「Kaibo for OBS Studio」。預設**保留**設定與登入，並會詢問是否一併刪除。也可以在面板的 **設定 → 刪除所有 Kaibo 資料** 隨時清除。Kaibo 寫入的檔案清單見 [docs/privacy.md](docs/privacy.md)。

## 致謝與授權

Kaibo 由 [Zarosmm/obs-bilibili-stream](https://github.com/Zarosmm/obs-bilibili-stream) 重構而來，感謝原作者與所有貢獻者。以 [GPL-2.0](LICENSE) 授權發佈。Kaibo 是獨立的開源工具，與嗶哩嗶哩官方無關。
