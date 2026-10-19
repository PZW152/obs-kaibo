# Changelog

## Unreleased

### Fixed
- Windows: the saved sign-in was sometimes rejected on the next OBS start ("stored sign-in could not be decrypted"), signing you out. The DPAPI entropy pointed at freed memory.
- The account card on the main page was stretched to the height of the settings page, leaving a large empty gap.
- The data folder in Settings is shown with native path separators.

## 1.0.0 — 2026-10-20

First release of Kaibo, a rewrite of obs-bilibili-stream 2.1.5.

### New
- Dock panel replaces the menu-and-dialog interface: QR sign-in inside the dock, account card with avatar and stats, room card, one Go Live button with live timer.
- One-click go live: syncs title / area / announcement, opens the room, writes the stream settings into OBS and starts streaming (optional).
- One-click end: stops OBS streaming and closes the room, with the broadcast duration.
- Status for every room × OBS combination, with the one action that fixes it; dropped streams never close the room.
- Area picker with search and recent areas; title history; room announcement editing.
- Optional: close the room when OBS exits, close it when you stop streaming in OBS, restore previous stream settings after ending.
- Face-verification QR inside OBS when Bilibili requires it.
- Hotkeys: “Kaibo: Go live”, “Kaibo: End live”.
- Simplified Chinese, Traditional Chinese and English; dark and light themes following OBS.
- Sign-in encrypted with Windows DPAPI; “Delete all Kaibo data” in settings; one-click import of the old plugin's sign-in.
- Windows installer with in-place upgrades and an uninstaller that keeps data unless you ask to delete it.

### Changed
- New name, logo, plugin id (`obs-kaibo`), Flatpak id (`com.obsproject.Studio.Plugin.Kaibo`) and data folder (`plugin_config/obs-kaibo`).
- Network code rewritten: HTTPS only, every request off the UI thread, form values URL-encoded, Bilibili errors shown with their code.
