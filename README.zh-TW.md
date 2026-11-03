<p align="center"><img src="docs/images/kaibo-lockup.png" width="300" alt="Kaibo 開播"></p>

<p align="center">
  <a href="https://github.com/PZW152/obs-kaibo/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/PZW152/obs-kaibo?color=d92d55&label=release"></a>
  <a href="https://github.com/PZW152/obs-kaibo/actions/workflows/push.yaml"><img alt="Build" src="https://img.shields.io/github/actions/workflow/status/PZW152/obs-kaibo/push.yaml?branch=main&label=build"></a>
  <a href="https://github.com/PZW152/obs-kaibo/actions/workflows/tests.yaml"><img alt="Tests" src="https://img.shields.io/github/actions/workflow/status/PZW152/obs-kaibo/tests.yaml?branch=main&label=tests"></a>
  <a href="https://github.com/PZW152/obs-kaibo/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/PZW152/obs-kaibo/total?color=111214"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/github/license/PZW152/obs-kaibo?color=111214"></a>
  <img alt="OBS 29.1+" src="https://img.shields.io/badge/OBS-29.1%2B-111214">
</p>

<p align="center"><b>在 OBS 裡一鍵開播嗶哩嗶哩。</b><br>掃碼登入 · 修改標題和分區 · 自動寫入串流設定 · 斷流提醒 · 简体 / 繁體 / English</p>

<p align="center"><a href="https://github.com/PZW152/obs-kaibo/releases/latest">下載</a> · <a href="README.md">简体中文</a> · <a href="README.en.md">English</a></p>

<p align="center"><img src="docs/images/dock-design.png" width="640" alt="Kaibo 面板設計稿"></p>

## 它做什麼

Kaibo 是 OBS Studio 的停駐面板（Dock）。登入一次之後，開播只要按一個按鈕：把修改過的標題、分區和公告同步到 B 站 → 開啟直播間 → 自動寫入 OBS 串流設定並開始串流。結束直播也是一個按鈕：停止串流、關閉直播間，並告訴你這場播了多久。

主要功能：面板內掃碼登入、分區搜尋與「最近使用」、標題歷史、直播間公告、**開播前檢查**（登入、直播間、分區、標題/公告長度、離線狀態）、斷流後一鍵重新串流、直播間與 OBS 狀態不一致時給出修復按鈕（只處理真正指向 B 站的串流，不會誤傷其他平台）、關閉 OBS 時自動關播、關播後還原原本的串流設定、人臉認證 QR 碼（完成後自動重試）、**工具 → 將面板放到右側全高**、快速鍵、跟隨 OBS 深色/淺色主題、簡體中文/繁體中文/English 介面、登入憑證以 Windows DPAPI 加密。

## 安裝

支援 **OBS Studio 29.1 以上**（Windows；已在 29.1.3 / 30.2.3 / 31.1.2 / 32.2.2 / 33.0.x 實測載入）。macOS 版本需要較新的 OBS（31.1+），Linux / Flatpak 與發行版或 runtime 的 OBS 相符，詳見 [docs/install.md](docs/install.md)。

- **Windows**：從 [Releases](https://github.com/PZW152/obs-kaibo/releases/latest) 下載 `obs-kaibo-<版本>-windows-x64-installer.exe`，關閉 OBS 後執行。外掛會安裝到 `C:\ProgramData\obs-studio\plugins\obs-kaibo`（Steam 版 OBS 也適用）。升級時直接執行新版安裝程式，設定與登入都會保留。
- **macOS**：安裝 `obs-kaibo-<版本>-macos-universal.pkg`。
- **Linux**：Ubuntu/Debian 使用 `.deb`；Flatpak 版 OBS 使用 `.flatpak`（`flatpak install --user ./obs-kaibo-*.flatpak`）。

開啟 OBS 後，Kaibo 面板會自動顯示；也可以從 **工具 → Kaibo** 開啟。全新安裝的 OBS 首次啟動時面板可能以浮動視窗出現，點 **工具 → 將面板放到右側全高** 即可歸位。要讓它佔滿右側整欄，請見[常見問題](docs/troubleshooting.md#想把面板放到最右侧占满整栏)。

## 解除安裝與資料

Windows 從「設定 → 應用程式」解除安裝「Kaibo for OBS Studio」。預設**保留**設定與登入，並會詢問是否一併刪除。也可以在面板的 **設定 → 刪除所有 Kaibo 資料** 隨時清除。Kaibo 寫入的檔案清單見 [docs/privacy.md](docs/privacy.md)。

## 致謝與授權

Kaibo 由 [Zarosmm/obs-bilibili-stream](https://github.com/Zarosmm/obs-bilibili-stream) 重構而來，感謝原作者與所有貢獻者。以 [GPL-2.0](LICENSE) 授權發佈。Kaibo 是獨立的開源工具，與嗶哩嗶哩官方無關。
