#!/usr/bin/env python3
"""Resume the official Sky PC payload when FeverGames' Intel IPC stalls."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.request


def home() -> Path:
    override = os.environ.get("YYB_INTEL_TEST_HOME")
    return Path(override).expanduser() if override else Path.home()


HOME = home()
SUPPORT = HOME / "Library/Application Support/YYBIntelLauncher"
PREFIX = HOME / "Library/Application Support/com.tencent.yybmac.wine.engine/wine"
SKY_DIR = PREFIX / "drive_c/FeverApps/sky"
USER_REG = PREFIX / "user.reg"
FEVER_LOG = (
    PREFIX
    / "drive_c/users/tencentyyb/AppData/Local/FeverGames/FeverGamesInstaller/logs/FeverGamesInstallerLog.txt"
)
MANIFEST_URL = (
    "https://loadingbaycn.webapp.163.com/app/v1/file_distribution/"
    "download_app?app_id=63&version=1"
)
DOWNLOAD_LOCK = SUPPORT / "sky-download.lock"
WATCH_LOCK = SUPPORT / "sky-download-watch.lock"
SYNC = SUPPORT / "bin/sync-launchpad-apps.py"
USER_AGENT = "sky-yyb-mac-fix-intel/1.0"
MANIFEST_ATTEMPTS = 4
MANIFEST_TIMEOUT = 45


class DownloadError(RuntimeError):
    pass


def atomic_write(path: Path, data: bytes) -> None:
    temporary = path.with_name(path.name + ".yyb-sky-new")
    temporary.write_bytes(data)
    if path.exists():
        temporary.chmod(path.stat().st_mode & 0o777)
    os.replace(temporary, path)


def stop_fever_processes() -> None:
    if os.environ.get("YYB_INTEL_TEST_HOME"):
        return
    try:
        output = subprocess.check_output(
            ["ps", "-axo", "pid=,command="], text=True, stderr=subprocess.DEVNULL
        )
    except (OSError, subprocess.CalledProcessError):
        return
    pids: list[int] = []
    for line in output.splitlines():
        fields = line.strip().split(maxsplit=1)
        if len(fields) != 2 or not fields[0].isdecimal():
            continue
        command = fields[1]
        if str(PREFIX) in command and "FeverGames" in command:
            pids.append(int(fields[0]))
    for pid in pids:
        try:
            os.kill(pid, 15)
        except (ProcessLookupError, PermissionError):
            pass
    if pids:
        time.sleep(1)


def upsert_game_value(text: str, name: str, value: str) -> str:
    section = r"Software\\FeverGames\\FeverGamesInstaller\\game\\63"
    header = re.compile(rf"(?m)^\[{re.escape(section)}\](?: [0-9]+)?$")
    match = header.search(text)
    if not match:
        raise DownloadError("网易启动器尚未建立光遇安装记录。")
    section_end = text.find("\n[", match.end())
    if section_end < 0:
        section_end = len(text)
    block = text[match.start() : section_end]
    line = f'"{name}"="{value}"'
    pattern = re.compile(rf'(?m)^"{re.escape(name)}"=.*$')
    if pattern.search(block):
        block = pattern.sub(lambda _match: line, block)
    else:
        block = block.rstrip("\n") + "\n" + line + "\n"
    return text[: match.start()] + block + text[section_end:]


def finalize_install(version: str) -> Path | None:
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,200}", version):
        raise DownloadError("网易版本号格式异常，拒绝写入。")
    sky = SKY_DIR / "Sky.exe"
    if not sky.is_file():
        sky = SKY_DIR / "sky.exe"
    if not sky.is_file() or not USER_REG.is_file():
        raise DownloadError("尚未找到完整的光遇程序或网易安装记录。")

    # FeverGames keeps DownloadVersionCode during transfer, but its UI decides
    # whether the game is launchable from VersionCode.  The old Intel IPC never
    # performs this final commit, even after every official file is present.
    stop_fever_processes()
    original = USER_REG.read_text(encoding="utf-8")
    updated = upsert_game_value(original, "VersionCode", version)
    updated = upsert_game_value(updated, "DownloadVersionCode", version)
    # FeverGames' normal downloader writes this top-level value after its
    # final IPC commit.  GameInfo also contains startup_path, but the launch
    # button's CreateProcess path is read from this registry value.
    updated = upsert_game_value(updated, "StartupPath", "Sky.exe")
    updated = upsert_game_value(updated, "StartupParams", "--start_from_launcher=1")
    updated = upsert_game_value(updated, "UpdateFlag", "false")
    if updated == original:
        return None
    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = SUPPORT / "backups" / f"sky-install-{stamp}"
    suffix = 0
    while backup.exists():
        suffix += 1
        backup = SUPPORT / "backups" / f"sky-install-{stamp}-{suffix:02d}"
    backup.mkdir(parents=True, mode=0o700)
    copy = backup / "user.reg"
    copy.write_text(original, encoding="utf-8")
    copy.chmod(0o600)
    atomic_write(USER_REG, updated.encode("utf-8"))
    return backup


def notify(title: str, message: str) -> None:
    if os.environ.get("YYB_INTEL_TEST_HOME"):
        return
    escaped_title = title.replace("\\", "\\\\").replace('"', '\\"')
    escaped_message = message.replace("\\", "\\\\").replace('"', '\\"')
    subprocess.run(
        [
            "/usr/bin/osascript",
            "-e",
            f'display notification "{escaped_message}" with title "{escaped_title}"',
        ],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def acquire_lock(path: Path) -> int | None:
    path.parent.mkdir(parents=True, exist_ok=True)
    for _attempt in range(2):
        try:
            descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            break
        except FileExistsError:
            try:
                pid = int(path.read_text(encoding="ascii"))
                os.kill(pid, 0)
                return None
            except (OSError, ValueError):
                path.unlink(missing_ok=True)
    else:
        return None
    os.write(descriptor, str(os.getpid()).encode("ascii"))
    return descriptor


def release_lock(path: Path, descriptor: int) -> None:
    os.close(descriptor)
    path.unlink(missing_ok=True)


def fetch_manifest() -> tuple[str, list[dict]]:
    request = urllib.request.Request(MANIFEST_URL, headers={"User-Agent": USER_AGENT})
    last_error: Exception | None = None
    for attempt in range(MANIFEST_ATTEMPTS):
        try:
            with urllib.request.urlopen(request, timeout=MANIFEST_TIMEOUT) as response:
                payload = json.load(response)
            content = payload["data"]["main_content"]
            version = content["version_code"]
            files = content["files"]
            break
        except (OSError, KeyError, ValueError, json.JSONDecodeError) as exc:
            last_error = exc
            if attempt + 1 < MANIFEST_ATTEMPTS:
                # NetEase's overseas CDN occasionally times out during the
                # TLS handshake. A bounded retry keeps a single explicit
                # launcher click from getting stuck on “loading”.
                time.sleep(2**attempt)
    else:
        raise DownloadError(
            f"无法读取网易官方文件清单（已重试 {MANIFEST_ATTEMPTS} 次）：{last_error}"
        ) from last_error
    if not isinstance(version, str) or not isinstance(files, list) or not files:
        raise DownloadError("网易官方文件清单格式异常。")
    return version, files


def target_for(relative: str) -> Path:
    pure = PurePosixPath(relative)
    if pure.is_absolute() or ".." in pure.parts or not pure.parts:
        raise DownloadError(f"官方清单包含不安全路径：{relative!r}")
    target = SKY_DIR.joinpath(*pure.parts)
    if not target.resolve(strict=False).is_relative_to(SKY_DIR.resolve(strict=False)):
        raise DownloadError(f"官方清单路径越界：{relative!r}")
    return target


def md5(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def complete_file(spec: dict) -> int:
    relative = spec.get("path")
    url = spec.get("url")
    expected_md5 = str(spec.get("md5", "")).lower()
    expected_size = spec.get("size")
    if (
        not isinstance(relative, str)
        or not isinstance(url, str)
        or not url.startswith("https://")
        or not isinstance(expected_size, int)
        or expected_size < 0
        or len(expected_md5) != 32
    ):
        raise DownloadError("网易官方文件清单包含无效记录。")
    target = target_for(relative)
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_file() and target.stat().st_size == expected_size:
        if md5(target) == expected_md5:
            return expected_size
    partial = target.with_name(target.name + ".yyb-download")
    if partial.exists() and partial.stat().st_size > expected_size:
        partial.unlink()
    if partial.exists() and partial.stat().st_size == expected_size:
        if md5(partial) == expected_md5:
            os.replace(partial, target)
            return expected_size
        partial.unlink()

    for attempt in range(4):
        offset = partial.stat().st_size if partial.exists() else 0
        headers = {"User-Agent": USER_AGENT}
        if offset:
            headers["Range"] = f"bytes={offset}-"
        request = urllib.request.Request(url, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=90) as response:
                append = offset > 0 and getattr(response, "status", None) == 206
                mode = "ab" if append else "wb"
                with partial.open(mode) as output:
                    shutil.copyfileobj(response, output, 4 * 1024 * 1024)
            if partial.stat().st_size != expected_size:
                raise DownloadError(
                    f"{relative} 大小不符：{partial.stat().st_size}/{expected_size}"
                )
            if md5(partial) != expected_md5:
                partial.unlink(missing_ok=True)
                raise DownloadError(f"{relative} MD5 校验失败")
            os.replace(partial, target)
            return expected_size
        except (OSError, DownloadError) as exc:
            if attempt == 3:
                raise DownloadError(f"下载 {relative} 失败：{exc}") from exc
            time.sleep(2**attempt)
    raise AssertionError("unreachable")


def download() -> int:
    descriptor = acquire_lock(DOWNLOAD_LOCK)
    if descriptor is None:
        print("光遇下载已经在后台运行。", flush=True)
        return 0
    try:
        version, files = fetch_manifest()
        total = sum(int(item.get("size", 0)) for item in files)
        free = shutil.disk_usage(SKY_DIR.parent if SKY_DIR.parent.exists() else PREFIX).free
        if free < total + 2 * 1024**3:
            raise DownloadError(
                f"空间不足：至少需要 {(total + 2 * 1024**3) / 1024**3:.1f} GB 可用空间。"
            )
        SKY_DIR.mkdir(parents=True, exist_ok=True)
        notify("光遇下载已接管", f"正从网易官方 CDN 下载 {total / 1024**3:.1f} GB，并自动校验。")
        print(f"光遇 {version}：{len(files)} 个文件，{total / 1024**3:.2f} GB", flush=True)
        completed = 0
        progress_lock = threading.Lock()
        with ThreadPoolExecutor(max_workers=4) as pool:
            futures = {pool.submit(complete_file, item): item for item in files}
            for future in as_completed(futures):
                size = future.result()
                with progress_lock:
                    completed += size
                    print(f"进度 {completed / total:6.1%}", flush=True)
        sky = SKY_DIR / "sky.exe"
        if not sky.is_file():
            sky = SKY_DIR / "Sky.exe"
        if not sky.is_file():
            raise DownloadError("文件已下载，但未找到 Sky.exe。")
        finalize_install(version)
        if SYNC.is_file():
            subprocess.run([str(SYNC)], check=False)
        notify("光遇下载完成", "文件已全部通过校验；重新打开网易启动器即可开始游戏。")
        print("光遇下载完成，所有文件已通过网易 MD5 校验。", flush=True)
        return 0
    except DownloadError as exc:
        notify("光遇下载未完成", str(exc))
        print(f"错误：{exc}", file=sys.stderr, flush=True)
        return 1
    finally:
        release_lock(DOWNLOAD_LOCK, descriptor)


def watch(seconds: int) -> int:
    descriptor = acquire_lock(WATCH_LOCK)
    if descriptor is None:
        return 0
    try:
        deadline = time.time() + seconds
        position = FEVER_LOG.stat().st_size if FEVER_LOG.exists() else 0
        pending = ""
        while time.time() < deadline:
            try:
                with FEVER_LOG.open("r", encoding="utf-8", errors="replace") as stream:
                    if stream.seek(0, os.SEEK_END) < position:
                        position = 0
                    stream.seek(position)
                    pending += stream.read()
                    position = stream.tell()
            except OSError:
                pass
            lines = pending.splitlines(keepends=True)
            pending = "" if not lines else ("" if lines[-1].endswith(("\n", "\r")) else lines.pop())
            if any(
                "onDownloadGame: 63" in line or "about to download game(63)" in line
                for line in lines
            ):
                return download()
            time.sleep(2)
        return 0
    finally:
        release_lock(WATCH_LOCK, descriptor)


def main() -> int:
    parser = argparse.ArgumentParser(description="修复 Intel 应用宝中光遇下载无响应")
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--watch", action="store_true")
    actions.add_argument("--finalize", action="store_true")
    parser.add_argument("--watch-seconds", type=int, default=4 * 60 * 60)
    args = parser.parse_args()
    if args.watch:
        return watch(args.watch_seconds)
    if args.finalize:
        try:
            version, _files = fetch_manifest()
            backup = finalize_install(version)
            if SYNC.is_file():
                subprocess.run([str(SYNC)], check=False)
            print(
                "网易启动器安装状态已修复。"
                + (f"备份在：{backup}" if backup else "无需重复修改。")
            )
            return 0
        except DownloadError as exc:
            print(f"错误：{exc}", file=sys.stderr)
            return 1
    return download()


if __name__ == "__main__":
    raise SystemExit(main())
