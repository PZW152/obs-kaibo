# Changelog

## 1.0.3 — 2026-10-25

### Fixed
- The replaced OBS stream service (which can contain another platform's stream key) is now sealed in `service-backup.bin` with the same protection as the sign-in and is no longer written to `settings.json`; a 1.0.2 plaintext backup is migrated on the first start.
- Atomic writes are flushed to disk before the rename, so a power loss cannot publish a partial settings/credentials file.
- The Windows installer falls back to `tasklist` when WMI is unavailable, so it still refuses to overwrite a DLL OBS has loaded.
- A JSON answer without a `code` field is no longer mistaken for success: that could sign the person out (the profile call read "not logged in") or report a live room as closed.
- HTTP 412 (Bilibili's risk-control answer) now shows Bilibili's own code and message instead of a bare "HTTP 412".
- Counts no longer round across a unit boundary ("1000K" is now "1M", "10000万" is now "1亿").
- Timestamps with impossible dates (e.g. 2026-02-31) are rejected instead of producing a wrong live duration.
- The replaced OBS stream service is captured fresh on every go-live; a backup left over from a crash can no longer overwrite the service the person configured since then and then restore the stale one at end-of-live.
- The captured service is no longer thrown away when the "restore previous stream settings" option is toggled while a broadcast is running.
- Closing OBS during a broadcast now restores the previous stream settings too (the option used to be silently skipped on that path).
- If Windows DPAPI cannot encrypt the sign-in, saving it now fails instead of silently writing the cookies to disk as plaintext.
- "Delete all Kaibo data" warns that the room is still live and that OBS may keep streaming.
- Failing to register the dock now stops the worker thread instead of leaking it.
- Going live no longer stalls on face verification: after the code is scanned, Kaibo waits and retries the start on its own (1/2/4/8/8 s) instead of immediately showing a fresh code, and says plainly when the requirement is Bilibili's per-account/area policy rather than something the plugin can skip.
- Start-live requests now carry Bilibili's device identity (`buvid3`/`buvid4`, fetched once and kept), sign with millisecond timestamps and no longer claim to come from the web live centre while also using the live-companion app key — the request identity is internally consistent again.
- `Set-Cookie` from API responses is now stored (only the QR sign-in step used to do that), so session and device cookies no longer drift away from what Bilibili handed out.
- The saved sign-in stores the cookies as they are after the request instead of the copy from before it.
- The face-verification prompt also accepts Bilibili's alternative `qrcode`/`url` answer fields and falls back to the verification page instead of a generic "could not start live" error.

### Changed
- Face verification is an in-dock card instead of an application-modal dialog: it no longer freezes OBS and can no longer stack into a nested loop of prompts.
- The Go Live button is really disabled while a start/stop/sync is running (it used to look active and swallow clicks); syncing shows "Saving…" in the status pill.
- Destructive confirmations (end broadcast, delete all data) keep the destructive choice off the Enter key.
- Long values (account name, room, data folder) wrap or get a tooltip in a narrow dock.
- Control sizes in the dock stylesheet come from the design tokens.

### Added
- Unit tests for the device-identity bootstrap and persistence, `Set-Cookie` absorption, the millisecond `ts` and client identity headers, the 60024 answer-field fallback, the face-verification retry schedule, missing envelopes, HTTP 412 risk control, count-unit boundaries, impossible dates and `0600` temp files.

## 1.0.2 — 2026-10-20

1.0.1 was tagged but never published; its fixes (below) ship in this release.

### Fixed
- Windows installer: running it while OBS was open failed with "Runtime error … Cannot call "UninstallSilent" function during Setup" instead of asking you to close OBS ([#2](https://github.com/PZW152/obs-kaibo/issues/2)). The uninstaller had the matching problem with `WizardSilent`.

## 1.0.1 — 2026-10-20 (not published)

### Fixed
- Windows: the saved sign-in was sometimes rejected on the next OBS start ("stored sign-in could not be decrypted"), signing you out. The DPAPI entropy pointed at freed memory.
- The account card on the main page was stretched to the height of the settings page, leaving a large empty gap.
- In a narrow dock, a long account name pushed the account card's buttons out of view.
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
