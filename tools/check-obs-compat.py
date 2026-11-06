#!/usr/bin/env python3
"""Checks the built Windows plugin against real OBS releases.

For each OBS version: downloads the official x64 zip, compares every symbol the plugin
imports with what that version's DLLs export, then starts OBS in portable mode and reads
its log to see whether the plugin loaded.

A missing import is proof the module cannot load, so it fails the check for any version
at or above --floor. A launch is only reported: a CI machine has no real GPU, so OBS may
stop before it loads plugins, and that says nothing about the plugin.

    python tools/check-obs-compat.py --plugin-dir <unpacked artifact> --work <dir> \\
        --floor 29.1.3 --versions 28.0.3 29.1.3 33.0.1

Needs `pip install pefile`. Set GH_TOKEN to avoid GitHub API rate limits.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

import pefile

RELEASES = "https://api.github.com/repos/obsproject/obs-studio/releases/tags/"
LAUNCH_SECONDS = 75


def version_key(version):
    return tuple(int(part) for part in version.split("."))


def fetch(url, accept=None):
    request = urllib.request.Request(url, headers={"User-Agent": "kaibo-compat-check"})
    if accept:
        request.add_header("Accept", accept)
    token = os.environ.get("GH_TOKEN")
    if token and "api.github.com" in url:
        request.add_header("Authorization", f"Bearer {token}")
    return urllib.request.urlopen(request, timeout=300)


def release_zip_url(version):
    with fetch(RELEASES + version, "application/vnd.github+json") as response:
        assets = json.load(response)["assets"]
    names = {asset["name"]: asset["browser_download_url"] for asset in assets}
    candidates = [
        name
        for name in names
        if name.endswith(".zip") and not re.search(r"pdb|arm64|mac|ubuntu|sources", name, re.I)
    ]
    if not candidates:
        raise RuntimeError(f"no Windows x64 zip among {sorted(names)}")
    return names[sorted(candidates, key=len)[0]]


def unpack_obs(version, work):
    target = work / f"obs-{version}"
    if (target / "bin" / "64bit" / "obs64.exe").exists():
        return target
    archive = work / f"obs-{version}.zip"
    with fetch(release_zip_url(version)) as response, open(archive, "wb") as out:
        shutil.copyfileobj(response, out)
    with zipfile.ZipFile(archive) as bundle:
        bundle.extractall(target)
    archive.unlink()
    # Some archives wrap everything in one top-level folder.
    if not (target / "bin").exists():
        inner = next(path.parent.parent.parent for path in target.rglob("obs64.exe"))
        for item in inner.iterdir():
            shutil.move(str(item), str(target / item.name))
    return target


def imports_of(path):
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
    table = {}
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        names = {item.name.decode() for item in entry.imports if item.name}
        table[entry.dll.decode().lower()] = names
    pe.close()
    return table


def exports_of(path):
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
    directory = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
    names = {item.name.decode() for item in directory.symbols if item.name} if directory else set()
    pe.close()
    return names


def missing_imports(plugin_imports, obs_root):
    bundled = {path.name.lower(): path for path in (obs_root / "bin" / "64bit").glob("*.dll")}
    missing = {}
    for dll, symbols in plugin_imports.items():
        if dll in bundled:
            gone = symbols - exports_of(bundled[dll])
            if gone:
                missing[dll] = sorted(gone)
        elif re.match(r"(qt6|obs|w32-pthreads)", dll):
            missing[dll] = ["<the DLL itself is not shipped>"]
    return missing


def launch(obs_root, plugin_dll, plugin_data):
    plugins = obs_root / "obs-plugins" / "64bit"
    plugins.mkdir(parents=True, exist_ok=True)
    shutil.copy2(plugin_dll, plugins)
    data = obs_root / "data" / "obs-plugins" / "obs-kaibo"
    shutil.rmtree(data, ignore_errors=True)
    shutil.copytree(plugin_data, data)
    (obs_root / "portable_mode.txt").write_text("")
    config = obs_root / "config" / "obs-studio"
    (config / "logs").mkdir(parents=True, exist_ok=True)

    process = subprocess.Popen(
        [
            str(obs_root / "bin" / "64bit" / "obs64.exe"),
            "--portable",
            "--multi",
            "--minimize-to-tray",
            "--disable-updater",
            "--disable-missing-files-check",
        ],
        cwd=obs_root / "bin" / "64bit",
    )
    outcome, detail = "no log", ""
    deadline = time.time() + LAUNCH_SECONDS
    while time.time() < deadline:
        time.sleep(3)
        logs = sorted((config / "logs").glob("*.txt"), key=lambda path: path.stat().st_mtime)
        if logs:
            text = logs[-1].read_text(encoding="utf-8", errors="replace")
            hit = re.search(r"\[obs-kaibo\] (loaded[^\r\n]*)", text)
            if hit:
                dock = re.search(r"dock 'kaibo' registered with (\S+)", text)
                outcome, detail = "loaded", dock.group(1) if dock else ""
                break
            failed = re.search(r"[^\r\n]*obs-kaibo[^\r\n]*(failed|not loaded|incompatible)[^\r\n]*", text, re.I)
            if failed:
                outcome, detail = "failed", failed.group(0).strip()[:160]
                break
            outcome = "no plugin line"
        if process.poll() is not None:
            outcome = f"{outcome}; OBS exited ({process.returncode})"
            break
    if process.poll() is None:
        process.kill()
        process.wait()
    return outcome, detail


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--plugin-dir", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--versions", nargs="+", required=True)
    parser.add_argument("--floor", help="oldest version that must have every import")
    parser.add_argument("--no-launch", action="store_true")
    args = parser.parse_args()

    plugin_dll = next(args.plugin_dir.rglob("obs-kaibo.dll"))
    plugin_data = plugin_dll.parent.parent.parent / "data"
    plugin_imports = imports_of(plugin_dll)
    args.work.mkdir(parents=True, exist_ok=True)
    print("Plugin imports:", ", ".join(f"{dll} ({len(names)})" for dll, names in sorted(plugin_imports.items())))

    rows, broken = [], False
    for version in args.versions:
        try:
            root = unpack_obs(version, args.work)
        except Exception as error:  # a missing release must not hide the other results
            rows.append((version, f"download failed: {error}", "-", ""))
            continue
        missing = missing_imports(plugin_imports, root)
        count = sum(len(names) for names in missing.values())
        for dll, names in missing.items():
            for name in names:
                print(f"{version}: {dll}: {name}")
        required = args.floor and version_key(version) >= version_key(args.floor)
        broken = broken or (bool(required) and count > 0)
        launched, detail = ("skipped", "")
        if not args.no_launch and os.name == "nt":
            launched, detail = launch(root, plugin_dll, plugin_data)
        rows.append((version, "ok" if not count else f"{count} missing", launched, detail))
        shutil.rmtree(root, ignore_errors=True)

    lines = ["| OBS | Imports | Launch | Detail |", "| --- | --- | --- | --- |"]
    lines += [f"| {version} | {imports} | {launched} | {detail} |" for version, imports, launched, detail in rows]
    report = "\n".join(lines) + "\n"
    print(report)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as out:
            out.write("## OBS compatibility\n\n" + report)
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
