#!/usr/bin/env python3
"""Install, repair and launch the NetEase China edition of Sky in YYB on macOS.

Only Python's standard library is used. The script never bundles or uploads
Tencent/NetEase binaries and never reads account tokens.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import time
import urllib.parse
import urllib.request
import zlib


PACKAGE_PARENT = "com.tencent.macexe.com.45a7ca33"
PACKAGE_SKY_PREFIX = PACKAGE_PARENT + "."
FEVER_API = "https://loadingbaycn.webapp.163.com/app/v1/download_client/windows"


def home() -> Path:
    override = os.environ.get("SKY_YYB_TEST_HOME")
    return Path(override).expanduser() if override else Path.home()


def applications_root() -> Path:
    override = os.environ.get("SKY_YYB_TEST_APPLICATIONS")
    return Path(override).expanduser() if override else Path("/Applications")


HOME = home()
APP_SUPPORT = HOME / "Library/Application Support"
YYB_DATA = APP_SUPPORT / "com.tencent.yybmac"
YYB_SERVICE = YYB_DATA / "helper/YYBService"
ENGINE_ROOT = APP_SUPPORT / "com.tencent.yybmac.wine.engine"
PREFIX = ENGINE_ROOT / "wine"
APPS_DB = ENGINE_ROOT / "apps/apps.db"
PUBLIC_MMKV = YYB_DATA / "publicMMKV/mmkv/com.tencent.yybmac.publicMMKV"
PUBLIC_MMKV_CRC = Path(str(PUBLIC_MMKV) + ".crc")
SKY_DIR = PREFIX / "drive_c/FeverApps/sky"
SKY_EXE = SKY_DIR / "Sky.exe"
PREFERENCES = SKY_DIR / "data/ThatGameCompany/com.netease.sky/preferences.sav"
USER_REG = PREFIX / "user.reg"
SYSTEM_REG = PREFIX / "system.reg"
STATE_ROOT = APP_SUPPORT / "SkyYYBMacFix"
LATEST_FILE = STATE_ROOT / "latest-backup.txt"
YYB_APP = applications_root() / "YYBMacApp.app"
YYB_SHORTCUTS = applications_root() / "腾讯应用宝"
YYB_INTERNAL_SHORTCUTS = YYB_DATA / "Applications"
LSREGISTER = Path(
    "/System/Library/Frameworks/CoreServices.framework/Frameworks/"
    "LaunchServices.framework/Support/lsregister"
)
SCRIPT_ROOT = Path(__file__).resolve().parent
GPU_COMPAT_SOURCE = SCRIPT_ROOT / "compat/apple_silicon_vulkan_compat.c"
GPU_COMPAT_DIR = STATE_ROOT / "compat"
GPU_COMPAT_DYLIB = GPU_COMPAT_DIR / "libSkyYYBGPUCompat.dylib"
SHORTCUT_WRAPPER_SOURCE = SCRIPT_ROOT / "compat/apple_silicon_shortcut_wrapper.c"
SHORTCUT_CONTROLLER_SOURCE = SCRIPT_ROOT / "compat/shortcut-controller.sh"
SHORTCUT_WRAPPER = GPU_COMPAT_DIR / "shortcut-wrapper"
SHORTCUT_CONTROLLER = GPU_COMPAT_DIR / "shortcut-controller.sh"
SHORTCUT_MARKER = b"SKY_YYB_SHORTCUT_WRAPPER_V1"
SHORTCUT_ORIGINAL_NAME = "YYBPackage.skyfix-original"
SHORTCUT_BACKUP_DIR = STATE_ROOT / "shortcut-originals"
NATIVE_ROOT = SCRIPT_ROOT / "native"
WINDOW_HOOK_SOURCE = NATIVE_ROOT / "yyb_wine_window_hook.m"
WINDOW_ADDRESS_SOURCE = NATIVE_ROOT / "yyb_dlopen_address.c"
WINDOW_WATCH_SOURCE = NATIVE_ROOT / "yyb_window_watch.sh"
WINDOW_INJECTOR_SOURCE = NATIVE_ROOT / "yyb_window_fix_win.c"
WINDOW_INJECTOR_BUNDLED = NATIVE_ROOT / "yyb-window-fix.exe"
WINDOW_ENGINE_ENTITLEMENTS = NATIVE_ROOT / "yyb-engine-entitlements.plist"
WINDOW_SUPPORT_BIN = STATE_ROOT / "bin"
WINDOW_SUPPORT_LIB = STATE_ROOT / "lib"
WINDOW_HOOK_DYLIB = WINDOW_SUPPORT_LIB / "libyyb-window-hook.dylib"
WINDOW_ADDRESS_HELPER = WINDOW_SUPPORT_BIN / "yyb-dlopen-address"
WINDOW_INJECTOR = WINDOW_SUPPORT_BIN / "yyb-window-fix.exe"
WINDOW_WATCH = WINDOW_SUPPORT_BIN / "yyb-window-watch"
WINDOW_WINELOADER = WINDOW_SUPPORT_BIN / "wineloader-yyb-helper"
WINDOW_LAUNCH_AGENT = HOME / "Library/LaunchAgents/com.skyyybmacfix.window.plist"
WINDOW_LAUNCH_LABEL = "com.skyyybmacfix.window"

# Tencent YYB Wine engine x86_64 wineloader builds. The copy used only by our
# injector crashes in build_path() when a directory argument is NULL on current
# macOS. Patch one exact instruction sequence in a private copy; the engine's
# installed executable is never modified.
WINDOW_WINELOADER_SHA256 = (
    "695024370e93310b447b24a60ba64ba34642f2ca3b589fde2659e7005c2f2b51"
)
WINDOW_WINELOADER_PATCH_OFFSET = 0x2D09
WINDOW_WINELOADER_124_SHA256 = (
    "f4ba5c0491717216ccc86f4d6ec9bbd5ce5894e8c1d95e39763f4bb92c43f05d"
)
WINDOW_WINELOADER_124_PATCH_OFFSET = 0x2D1F
WINDOW_WINELOADER_ORIGINAL = bytes.fromhex(
    "48 8b 7d f0 48 8b 45 f8 ff d0 48 89 45 e0 48 8b 45 e0"
)
WINDOW_WINELOADER_REPLACEMENT = bytes.fromhex(
    "31 c0 48 8b 7d f0 48 85 ff 74 03 ff 55 f8 48 89 45 e0"
)
WINDOW_WINELOADER_BUILDS = (
    (WINDOW_WINELOADER_SHA256, WINDOW_WINELOADER_PATCH_OFFSET),
    (WINDOW_WINELOADER_124_SHA256, WINDOW_WINELOADER_124_PATCH_OFFSET),
)

# Tencent YYB Wine engine 1.10.41, as shipped by YYB macOS 0.8.0 (Build 2140).
# No Tencent binary is distributed by this project.  The installer recognizes
# the exact local binary, verifies every instruction sequence it replaces, and
# writes the transformed image only after BackupSet has captured the original.
M2_WINEVULKAN_ORIGINAL_SHA256 = (
    "d4e0c5fd2320c8cc02d509b6363978c2bc4ce90c88a28ab2af48087102036615"
)
M2_WINEVULKAN_PATCHED_SHA256 = (
    "084b97a5a02dc5dfdb65a15a85d85fd0ea0b3ec14ff7c0fcea1b2c031aa69f0a"
)
WINEVULKAN_123_ORIGINAL_SHA256 = (
    "a7ae56dd7a3264f91d7980002e9f4b241d795237a81b23f1f4bf46233f654366"
)
WINEVULKAN_123_PATCHED_SHA256 = (
    "27f5aeb578090b2a9a681530ef193dbcc8e6ab03a963406d341b7b65dd7ac98e"
)
WINEVULKAN_124_ORIGINAL_SHA256 = (
    "4e110bfec5683750501d499bad7d3229b2454fe5a3fbe361ae4702597c194218"
)
WINEVULKAN_124_PATCHED_SHA256 = (
    "3d47fd2957cdac3f28e8539ab3eddae45540cc40ec5fd9c898f81a2e8b5f4009"
)
M2_WINEVULKAN_RELATIVE = Path(
    "ExeEngineDownload/wine-engine.app/Contents/SharedSupport/wine/lib/"
    "wine/x86_64-windows/winevulkan.dll"
)
WINEVULKAN_ORIGINALS_DIR = STATE_ROOT / "vulkan-originals"

# Each tuple is (file offset, exact original bytes, replacement bytes).  These
# patches affect Wine's Vulkan reporting/creation bridge, not Sky.exe:
#   1. report geometryShader in VkPhysicalDeviceFeatures/Features2;
#   2. expose a desktop-GPU identity accepted by this Sky build;
#   3. clear the unsupported geometryShader request immediately before Wine
#      forwards vkCreateDevice to MoltenVK.
M2_WINEVULKAN_PATCHES = (
    (
        0x2C338,
        bytes.fromhex("85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 5e 26 04 00 48"),
        bytes.fromhex("48 8b 44 24 30 c7 40 10 01 00 00 00 48 83 c4 38 5f 5e c3"),
    ),
    (
        0x2C448,
        bytes.fromhex("85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 7e 25 04 00 48"),
        bytes.fromhex("48 8b 44 24 30 c7 40 20 01 00 00 00 48 83 c4 38 5f 5e c3"),
    ),
    (
        0x2D468,
        bytes.fromhex(
            "85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 ca 18 04 00 48 89 "
            "44 24 20 48 8d 35 92 18 04 00 48 8d 15"
        ),
        bytes.fromhex(
            "48 8b 44 24 30 c7 40 08 de 10 00 00 c7 40 0c 03 1c 00 00 "
            "c7 40 10 02 00 00 00 48 83 c4 38 5f 5e c3"
        ),
    ),
    (
        0x2D578,
        bytes.fromhex(
            "85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 ea 17 04 00 48 89 "
            "44 24 20 48 8d 35 b2 17 04 00 48 8d 15"
        ),
        bytes.fromhex(
            "48 8b 44 24 30 c7 40 18 de 10 00 00 c7 40 1c 03 1c 00 00 "
            "c7 40 20 02 00 00 00 48 83 c4 38 5f 5e c3"
        ),
    ),
    (
        0x1CA80,
        bytes.fromhex("41 57 41 56 41 55"),
        bytes.fromhex("e9 7b a2 01 00 90"),
    ),
    (
        0x36D00,
        bytes(43),
        bytes.fromhex(
            "41 57 41 56 41 55 48 8b 42 08 48 85 c0 74 07 c7 40 20 00 00 "
            "00 00 48 8b 42 40 48 85 c0 74 07 c7 40 10 00 00 00 00 e9 "
            "5b 5d fe ff"
        ),
    ),
)

# YYB Wine engine 1.2.3 (Build 690) retains the same exported Vulkan ABI but
# moves the relevant functions. The preimages were checked against its local PE
# export table and disassembly. Its VkDeviceCreateInfo hook clears the same
# feature fields as the older, working engine.
WINEVULKAN_123_PATCHES = (
    (
        0x2C0B6,
        bytes.fromhex("85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 e4 28 04 00 48"),
        M2_WINEVULKAN_PATCHES[0][2],
    ),
    (
        0x2C1B6,
        bytes.fromhex("85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 14 28 04 00 48"),
        M2_WINEVULKAN_PATCHES[1][2],
    ),
    (
        0x2D186,
        bytes.fromhex(
            "85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 b0 1b 04 00 48 89 "
            "44 24 20 48 8d 35 78 1b 04 00 48 8d 15"
        ),
        M2_WINEVULKAN_PATCHES[2][2],
    ),
    (
        0x2D286,
        bytes.fromhex(
            "85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 e0 1a 04 00 48 89 "
            "44 24 20 48 8d 35 a8 1a 04 00 48 8d 15"
        ),
        M2_WINEVULKAN_PATCHES[3][2],
    ),
    (0x1C930, bytes.fromhex("41 57 41 56 41 55"), bytes.fromhex("e9 cb a3 01 00 90")),
    (
        0x36D00,
        bytes(43),
        bytes.fromhex(
            "41 57 41 56 41 55 48 8b 42 08 48 85 c0 74 07 c7 40 20 00 00 "
            "00 00 48 8b 42 40 48 85 c0 74 07 c7 40 10 00 00 00 00 e9 "
            "0b 5c fe ff"
        ),
    ),
)

# YYB Wine engine 1.2.4 (Build 713) keeps the same wrappers and compatibility
# requirements as 1.2.3, but updates Wine and shifts the exported functions.
# The trampoline lives in zero padding at the executable .text section tail;
# both the complete PE hash and every preimage remain mandatory.
WINEVULKAN_124_PATCHES = (
    (
        0x2CB7C,
        bytes.fromhex("85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 1a 2e 04 00 48"),
        M2_WINEVULKAN_PATCHES[0][2],
    ),
    (
        0x2CC8C,
        bytes.fromhex("85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 3a 2d 04 00 48"),
        M2_WINEVULKAN_PATCHES[1][2],
    ),
    (
        0x2DCCC,
        bytes.fromhex(
            "85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 66 20 04 00 48 89 "
            "44 24 20 48 8d 35 2e 20 04 00 48 8d 15"
        ),
        M2_WINEVULKAN_PATCHES[2][2],
    ),
    (
        0x2DDDC,
        bytes.fromhex(
            "85 c0 75 07 48 83 c4 38 5f 5e c3 48 8d 05 86 1f 04 00 48 89 "
            "44 24 20 48 8d 35 4e 1f 04 00 48 8d 15"
        ),
        M2_WINEVULKAN_PATCHES[3][2],
    ),
    (0x1CF10, bytes.fromhex("41 57 41 56 41 55"), bytes.fromhex("e9 3b a8 01 00 90")),
    (
        0x37750,
        bytes(43),
        bytes.fromhex(
            "41 57 41 56 41 55 48 8b 42 08 48 85 c0 74 07 c7 40 20 00 00 "
            "00 00 48 8b 42 40 48 85 c0 74 07 c7 40 10 00 00 00 00 e9 "
            "9b 57 fe ff"
        ),
    ),
)

WINEVULKAN_BUILDS = (
    (M2_WINEVULKAN_ORIGINAL_SHA256, M2_WINEVULKAN_PATCHED_SHA256, M2_WINEVULKAN_PATCHES),
    (WINEVULKAN_123_ORIGINAL_SHA256, WINEVULKAN_123_PATCHED_SHA256, WINEVULKAN_123_PATCHES),
    (WINEVULKAN_124_ORIGINAL_SHA256, WINEVULKAN_124_PATCHED_SHA256, WINEVULKAN_124_PATCHES),
)


class FixError(RuntimeError):
    pass


def say(message: str) -> None:
    print(message, flush=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def detected_chip() -> str:
    override = os.environ.get("SKY_YYB_TEST_CHIP")
    if override is not None:
        return override
    if os.environ.get("SKY_YYB_TEST_HOME"):
        return ""
    if sys.platform != "darwin" or platform.machine() != "arm64":
        return ""
    try:
        result = subprocess.run(
            ["system_profiler", "SPHardwareDataType", "-json"],
            check=True,
            capture_output=True,
            text=True,
            timeout=20,
        )
        payload = json.loads(result.stdout)
        records = payload.get("SPHardwareDataType", [])
        if records:
            return str(records[0].get("chip_type", ""))
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError):
        pass
    return ""


def patch_m2_winevulkan_image(data: bytes) -> bytes:
    """Return the reviewed M2/M4 compatibility transform for engine 1.10.41."""
    return patch_winevulkan_image(data, M2_WINEVULKAN_PATCHES)


def patch_winevulkan_image(data: bytes, patches: tuple) -> bytes:
    """Apply only byte sequences verified for one exact Wine Vulkan build."""
    patched = bytearray(data)
    for offset, original, replacement in patches:
        if len(original) != len(replacement):
            raise AssertionError("Vulkan patch must preserve the PE image layout")
        existing = bytes(patched[offset:offset + len(original)])
        if existing == replacement:
            continue
        if existing != original:
            raise FixError(
                f"Vulkan 组件在偏移 {offset:#x} 与已验证版本不符，拒绝修改。"
            )
        patched[offset:offset + len(replacement)] = replacement
    return bytes(patched)


def winevulkan_targets() -> list[Path]:
    targets = [
        YYB_DATA / M2_WINEVULKAN_RELATIVE,
        PREFIX / "drive_c/windows/system32/winevulkan.dll",
        SKY_DIR / "winevulkan.dll",
    ]
    return list(dict.fromkeys(path for path in targets if path.exists()))


def winevulkan_original_backup(path: Path) -> Path:
    identity = hashlib.sha256(str(path).encode("utf-8")).hexdigest()[:16]
    return WINEVULKAN_ORIGINALS_DIR / f"{identity}.winevulkan.dll"


def find_historical_winevulkan_original(path: Path, original_sha: str) -> bytes | None:
    backups = STATE_ROOT / "backups"
    if not backups.exists():
        return None
    for root in sorted(backups.iterdir(), reverse=True):
        manifest = root / "manifest.json"
        try:
            items = json.loads(manifest.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        for item in items:
            if item.get("path") != str(path) or not item.get("existed"):
                continue
            source = root / str(item.get("backup", ""))
            if source.exists() and sha256(source) == original_sha:
                return source.read_bytes()
    return None


def preserve_winevulkan_original(
    path: Path, original_sha: str, patches: tuple, data: bytes | None = None
) -> None:
    backup = winevulkan_original_backup(path)
    if backup.exists() and sha256(backup) == original_sha:
        return
    if data is None:
        data = find_historical_winevulkan_original(path, original_sha)
    if data is None:
        # A known patched image contains every original byte except the
        # enumerated substitutions. Reconstruct and hash-check the original
        # when old transaction backups have already been removed.
        reverse = tuple((offset, replacement, original) for offset, original, replacement in patches)
        data = patch_winevulkan_image(path.read_bytes(), reverse)
    if hashlib.sha256(data).hexdigest() != original_sha:
        raise FixError(f"找不到经过校验的 Wine Vulkan 原版备份：{path}")
    ensure_private_dir(WINEVULKAN_ORIGINALS_DIR)
    atomic_write(backup, data, 0o600)


def restore_winevulkan_originals() -> int:
    restored = 0
    for path in winevulkan_targets():
        build = next((item for item in WINEVULKAN_BUILDS if sha256(path) == item[1]), None)
        if build is None:
            continue
        original_sha, _patched_sha, patches = build
        preserve_winevulkan_original(path, original_sha, patches)
        backup = winevulkan_original_backup(path)
        if not backup.exists() or sha256(backup) != original_sha:
            raise FixError(f"Wine Vulkan 原版恢复副本缺失或校验失败：{path}")
        atomic_write(path, backup.read_bytes())
        if sha256(path) != original_sha:
            raise FixError(f"Wine Vulkan 原版恢复后校验失败：{path}")
        restored += 1
    return restored


def patch_m2_vulkan_compat(backups: "BackupSet") -> list[str]:
    chip = detected_chip()
    if chip not in ("Apple M2", "Apple M4"):
        return []

    targets = winevulkan_targets()
    engine_dll = YYB_DATA / M2_WINEVULKAN_RELATIVE
    if engine_dll not in targets:
        raise FixError("未找到应用宝的 Wine Vulkan 组件，无法应用 Apple Silicon 修复。")

    changed = 0
    verified = 0
    for path in targets:
        digest = sha256(path)
        build = next((item for item in WINEVULKAN_BUILDS if digest in item[:2]), None)
        if build is None:
            raise FixError(
                "检测到未验证的 Wine Vulkan 版本，已停止以免损坏应用宝："
                f"{path}（SHA-256 {digest[:16]}…）"
            )
        original_sha, patched_sha, patches = build
        if digest == patched_sha:
            preserve_winevulkan_original(path, original_sha, patches)
            verified += 1
            continue
        original = path.read_bytes()
        preserve_winevulkan_original(path, original_sha, patches, original)
        transformed = patch_winevulkan_image(original, patches)
        if hashlib.sha256(transformed).hexdigest() != patched_sha:
            raise FixError("Apple Silicon Vulkan 修补结果校验失败，未写入。")
        backups.capture(path)
        atomic_write(path, transformed)
        if sha256(path) != patched_sha:
            raise FixError("Apple Silicon Vulkan 修补写入后校验失败。")
        changed += 1

    # A DLL beside Sky.exe wins Wine's normal search order over system32.
    # YYB leaves that private copy behind when it updates the engine, which
    # otherwise makes the game load an older Wine PE half with a newer engine.
    # Keep every runtime copy on the exact, hash-pinned build installed by the
    # current engine. This is particularly important after YYB engine updates.
    engine_digest = sha256(engine_dll)
    engine_build = next(
        (item for item in WINEVULKAN_BUILDS if engine_digest == item[1]),
        None,
    )
    if engine_build is None:
        raise FixError("应用宝当前 Wine Vulkan 引擎没有处于已验证的修补状态。")
    synchronized = 0
    engine_image = engine_dll.read_bytes()
    for path in targets:
        if path == engine_dll or sha256(path) == engine_digest:
            continue
        backups.capture(path)
        atomic_write(path, engine_image)
        if sha256(path) != engine_digest:
            raise FixError(f"Wine Vulkan 运行时版本同步失败：{path}")
        synchronized += 1

    return [
        f"{chip} Vulkan 设备识别/geometryShader/vkCreateDevice 兼容修复"
        f"（修改 {changed}、已存在 {verified}、版本同步 {synchronized}）"
    ]


def verified_winevulkan_patch_active() -> bool:
    if detected_chip() not in ("Apple M2", "Apple M4"):
        return False
    engine_dll = YYB_DATA / M2_WINEVULKAN_RELATIVE
    if not engine_dll.exists():
        return False
    engine_digest = sha256(engine_dll)
    if engine_digest not in {item[1] for item in WINEVULKAN_BUILDS}:
        return False
    return all(sha256(path) == engine_digest for path in winevulkan_targets())


def ensure_private_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)
    path.chmod(0o700)


class BackupSet:
    def __init__(self) -> None:
        ensure_private_dir(STATE_ROOT)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        self.root = STATE_ROOT / "backups" / stamp
        ensure_private_dir(self.root)
        self.manifest: list[dict[str, str | bool | int]] = []

    def capture(self, path: Path) -> None:
        if any(item["path"] == str(path) for item in self.manifest):
            return
        relative = f"item-{len(self.manifest):03d}"
        target = self.root / relative
        existed = path.exists()
        if existed:
            shutil.copy2(path, target)
            target.chmod(0o600)
        item: dict[str, str | bool | int] = {
            "path": str(path),
            "backup": relative,
            "existed": existed,
        }
        if existed:
            # Backups themselves stay private (0600), but remember the source
            # mode so restoring a Mach-O/dylib does not silently remove its
            # executable bit.
            item["mode"] = path.stat().st_mode & 0o777
        self.manifest.append(item)

        # Keep the manifest crash-safe as each item is captured, so a failed
        # write later in the transaction can still be rolled back immediately.
        self._persist_manifest()

    def _persist_manifest(self) -> None:
        manifest = self.root / "manifest.json"
        manifest.write_text(
            json.dumps(self.manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        manifest.chmod(0o600)

    def finish(self) -> None:
        self._persist_manifest()
        LATEST_FILE.write_text(str(self.root) + "\n", encoding="utf-8")
        LATEST_FILE.chmod(0o600)


def atomic_write(path: Path, data: bytes, mode: int | None = None) -> None:
    tmp = path.with_name(path.name + ".skyfix-new")
    tmp.write_bytes(data)
    if mode is None and path.exists():
        mode = path.stat().st_mode & 0o777
    if mode is not None:
        tmp.chmod(mode)
    os.replace(tmp, path)


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise FixError(f"无法读取 {path}: {exc}") from exc


def find_fever_launcher() -> Path | None:
    candidate = PREFIX / "drive_c/Program Files/FeverGames/FeverGamesLauncher.exe"
    return candidate if candidate.exists() else None


def installed_fever_versions() -> list[Path]:
    base = PREFIX / "drive_c/Program Files/FeverGames"
    if not base.exists():
        return []
    return sorted(
        (item for item in base.iterdir() if item.is_dir() and re.fullmatch(r"[0-9.]+", item.name)),
        key=lambda item: tuple(int(part) for part in item.name.split(".")),
    )


def find_engine_app() -> Path | None:
    base = YYB_DATA / "ExeEngineDownload"
    if not base.exists():
        return None
    candidates = []
    for app in base.glob("*.app"):
        wineserver = app / "Contents/MacOS/wineserver"
        moltenvk = app / "Contents/Frameworks/libMoltenVK.dylib"
        if wineserver.exists() and moltenvk.exists():
            candidates.append(app)
    return max(candidates, key=lambda item: item.stat().st_mtime) if candidates else None


def patch_window_wineloader_image(data: bytes) -> bytes:
    """Return the verified NULL-safe image used only by the injector."""
    digest = hashlib.sha256(data).hexdigest()
    build = next((item for item in WINDOW_WINELOADER_BUILDS if item[0] == digest), None)
    if not build:
        raise FixError(
            "应用宝 wineloader 版本未经验证，拒绝为窗口修复创建辅助副本"
            f"（SHA-256 {digest[:16]}…）。"
        )
    start = build[1]
    end = start + len(WINDOW_WINELOADER_ORIGINAL)
    if data[start:end] != WINDOW_WINELOADER_ORIGINAL:
        raise FixError("应用宝 wineloader 的窗口辅助补丁位置不匹配，拒绝修改。")
    result = bytearray(data)
    result[start:end] = WINDOW_WINELOADER_REPLACEMENT
    return bytes(result)


def window_launch_agent_payload() -> dict:
    """Build a per-user launchd job without hard-coded home-directory paths."""
    return {
        "Label": WINDOW_LAUNCH_LABEL,
        "ProgramArguments": [str(WINDOW_WATCH)],
        "RunAtLoad": True,
        "KeepAlive": True,
        "ThrottleInterval": 10,
        "ProcessType": "Background",
        "StandardOutPath": str(STATE_ROOT / "window-fix.log"),
        "StandardErrorPath": str(STATE_ROOT / "window-fix.err"),
    }


def _native_build(command: list[str], description: str) -> None:
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode:
        detail = (result.stderr or result.stdout).strip().splitlines()
        raise FixError(
            f"{description}失败：" + (detail[-1] if detail else f"错误码 {result.returncode}")
        )


def install_fever_window_fix() -> list[str]:
    """Install the owner-scoped native geometry fix for NetEase Fever.

    Fever's Wine window contains 91 points of invisible decoration. Moving
    only its WindowServer surface makes pixels and hit testing disagree. The
    injected hook instead updates Wine's cached frame, then exempts only the
    real Fever WineWindow from AppKit's private menu-bar avoidance routine.
    """
    if os.environ.get("SKY_YYB_TEST_HOME") or detected_chip() not in ("Apple M2", "Apple M4"):
        return []
    required = (
        WINDOW_HOOK_SOURCE,
        WINDOW_ADDRESS_SOURCE,
        WINDOW_WATCH_SOURCE,
        WINDOW_INJECTOR_SOURCE,
        WINDOW_INJECTOR_BUNDLED,
        WINDOW_ENGINE_ENTITLEMENTS,
    )
    missing = next((path for path in required if not path.exists()), None)
    if missing:
        raise FixError(f"网易启动器窗口修复组件缺失：{missing}")
    compiler = Path("/usr/bin/clang")
    if not compiler.exists():
        raise FixError("需要 Apple clang 编译网易启动器窗口修复；请先安装 Xcode Command Line Tools。")
    engine = find_engine_app()
    if not engine:
        raise FixError("未找到应用宝 Wine 引擎，无法安装网易启动器窗口修复。")
    engine_contents = engine / "Contents"
    source_loader = engine_contents / "MacOS/wineloader"
    if not source_loader.exists():
        raise FixError(f"应用宝 Wine 加载器不存在：{source_loader}")

    ensure_private_dir(WINDOW_SUPPORT_BIN)
    ensure_private_dir(WINDOW_SUPPORT_LIB)
    build_signature = hashlib.sha256(
        WINDOW_HOOK_SOURCE.read_bytes()
        + WINDOW_ADDRESS_SOURCE.read_bytes()
        + WINDOW_WATCH_SOURCE.read_bytes()
        + WINDOW_INJECTOR_SOURCE.read_bytes()
        + WINDOW_INJECTOR_BUNDLED.read_bytes()
        + WINDOW_ENGINE_ENTITLEMENTS.read_bytes()
        + source_loader.read_bytes()
    ).hexdigest()
    stamp = STATE_ROOT / "window-build.json"
    try:
        current = json.loads(stamp.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        current = {}
    expected_assets = (
        WINDOW_HOOK_DYLIB,
        WINDOW_ADDRESS_HELPER,
        WINDOW_INJECTOR,
        WINDOW_WATCH,
        WINDOW_WINELOADER,
    )
    rebuild = current.get("signature") != build_signature or not all(
        path.exists() for path in expected_assets
    )

    if rebuild:
        hook_new = WINDOW_HOOK_DYLIB.with_name(WINDOW_HOOK_DYLIB.name + ".new")
        address_new = WINDOW_ADDRESS_HELPER.with_name(WINDOW_ADDRESS_HELPER.name + ".new")
        loader_new = WINDOW_WINELOADER.with_name(WINDOW_WINELOADER.name + ".new")
        for temporary in (hook_new, address_new, loader_new):
            temporary.unlink(missing_ok=True)
        _native_build(
            [
                str(compiler), "-arch", "x86_64", "-Os", "-dynamiclib", "-fobjc-arc",
                "-Wall", "-Wextra", "-Werror", "-framework", "AppKit",
                "-framework", "Foundation", "-o", str(hook_new),
                str(WINDOW_HOOK_SOURCE),
            ],
            "网易启动器窗口 Hook 编译",
        )
        _native_build(
            [
                str(compiler), "-arch", "x86_64", "-Os", "-Wall", "-Wextra",
                "-Werror", "-o", str(address_new), str(WINDOW_ADDRESS_SOURCE),
            ],
            "网易启动器地址助手编译",
        )
        for output in (hook_new, address_new):
            output.chmod(0o700)
            _native_build(
                ["/usr/bin/codesign", "--force", "--sign", "-", str(output)],
                f"{output.name} 本机签名",
            )

        loader_new.write_bytes(patch_window_wineloader_image(source_loader.read_bytes()))
        loader_new.chmod(0o700)
        _native_build(
            [
                "/usr/bin/codesign", "--force", "--sign", "-", "--options", "runtime",
                "--entitlements", str(WINDOW_ENGINE_ENTITLEMENTS), str(loader_new),
            ],
            "网易启动器注入加载器本机签名",
        )
        os.replace(hook_new, WINDOW_HOOK_DYLIB)
        os.replace(address_new, WINDOW_ADDRESS_HELPER)
        os.replace(loader_new, WINDOW_WINELOADER)
        atomic_write(WINDOW_INJECTOR, WINDOW_INJECTOR_BUNDLED.read_bytes(), 0o700)
        atomic_write(WINDOW_WATCH, WINDOW_WATCH_SOURCE.read_bytes(), 0o700)
        atomic_write(
            stamp,
            (json.dumps({"signature": build_signature}, indent=2) + "\n").encode("utf-8"),
            0o600,
        )

    WINDOW_LAUNCH_AGENT.parent.mkdir(parents=True, exist_ok=True)
    agent = plistlib.dumps(
        window_launch_agent_payload(), fmt=plistlib.FMT_XML, sort_keys=False
    )
    atomic_write(WINDOW_LAUNCH_AGENT, agent, 0o600)
    domain = f"gui/{os.getuid()}"
    subprocess.run(
        ["/bin/launchctl", "bootout", domain, str(WINDOW_LAUNCH_AGENT)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    result = subprocess.run(
        ["/bin/launchctl", "bootstrap", domain, str(WINDOW_LAUNCH_AGENT)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode:
        detail = (result.stderr or result.stdout).strip()
        raise FixError("网易启动器窗口修复服务启用失败：" + (detail or str(result.returncode)))
    return ["网易启动器动态最大化、顶部空白、拖拽跳位与点击坐标同步修复"]


def fever_window_fix_installed() -> bool:
    if detected_chip() not in ("Apple M2", "Apple M4"):
        return False
    return WINDOW_LAUNCH_AGENT.exists() and all(
        path.exists()
        for path in (
            WINDOW_HOOK_DYLIB,
            WINDOW_ADDRESS_HELPER,
            WINDOW_INJECTOR,
            WINDOW_WATCH,
            WINDOW_WINELOADER,
        )
    )


def engine_wineserver() -> Path | None:
    app = find_engine_app()
    return app / "Contents/MacOS/wineserver" if app else None


def is_apple_m4() -> bool:
    if platform.machine() != "arm64" or os.environ.get("SKY_YYB_TEST_HOME"):
        return False
    result = subprocess.run(
        ["sysctl", "-n", "machdep.cpu.brand_string"],
        capture_output=True,
        text=True,
        check=False,
    )
    return result.returncode == 0 and result.stdout.strip().startswith("Apple M4")


def shortcut_roots() -> tuple[Path, ...]:
    # YYB buttons invoke the private source bundle; the /Applications copy is
    # only the user-facing mirror. Prefer the same source YYB itself uses.
    return (YYB_INTERNAL_SHORTCUTS, YYB_SHORTCUTS)


def all_shortcuts_for(package_name: str) -> list[Path]:
    matches: list[Path] = []
    for root in shortcut_roots():
        direct = root / f"{package_name}.app"
        if direct.exists():
            matches.append(direct)
            continue
        if not root.exists():
            continue
        for app in root.glob("*.app"):
            if shortcut_package(app) == package_name:
                matches.append(app)
                break
    return matches


def shortcut_for(package_name: str) -> Path | None:
    matches = all_shortcuts_for(package_name)
    return matches[0] if matches else None


def shortcut_package(app: Path) -> str | None:
    plist = app / "Contents/Info.plist"
    try:
        with plist.open("rb") as stream:
            value = plistlib.load(stream).get("YYBPackageName")
    except (OSError, plistlib.InvalidFileException):
        return None
    return value if isinstance(value, str) else None


def is_shortcut_wrapper(path: Path) -> bool:
    try:
        return SHORTCUT_MARKER in path.read_bytes()
    except OSError:
        return False


def shortcut_backup(app: Path, package: str) -> Path:
    location = hashlib.sha256(str(app).encode("utf-8")).hexdigest()[:16]
    return SHORTCUT_BACKUP_DIR / location / f"{package}.YYBPackage"


def stop_related_processes() -> None:
    if os.environ.get("SKY_YYB_TEST_HOME"):
        return
    # Exact executable names only. Failures are harmless when nothing is open.
    for name in (
        "Sky.exe",
        "FeverGamesWeb.exe",
        "FeverGamesWeb",
        "FeverGamesInstaller.exe",
        "FeverGamesLauncher.exe",
        "YYBPackage",
        "YYBMacApp",
        "YYBService",
        "LaunchPad",
    ):
        subprocess.run(
            ["pkill", "-x", name],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    if YYB_SHORTCUTS.exists():
        for executable in YYB_SHORTCUTS.glob("*.app/Contents/MacOS/YYBPackage"):
            subprocess.run(
                ["pkill", "-f", str(executable)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
    # Wine launchers appear under truncated Windows paths in macOS process
    # listings, so name-based pkill does not actually stop them. Ask the YYB
    # wineserver to close the whole prefix before editing its live databases.
    wineserver = engine_wineserver()
    if wineserver:
        environment = os.environ.copy()
        environment["WINEPREFIX"] = str(PREFIX)
        subprocess.run(
            [str(wineserver), "-k"],
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    time.sleep(1)


def repair_yyb_signature() -> bool:
    """Repair a locally broken YYB bundle signature on newer macOS builds.

    Some YYB self-updates leave every Mach-O in the app with a stale resource
    seal. LaunchServices then reports kLSNoExecutableErr even though the main
    executable exists. Keep one complete local backup and ad-hoc sign strictly
    from the deepest code objects outward. Account/game data lives outside the
    app bundle and is not touched.
    """
    if os.environ.get("SKY_YYB_TEST_HOME") or sys.platform != "darwin":
        return False
    verify = subprocess.run(
        ["/usr/bin/codesign", "--verify", "--deep", "--strict", str(YYB_APP)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    repair_app = verify.returncode != 0

    info_path = YYB_APP / "Contents/Info.plist"
    try:
        with info_path.open("rb") as stream:
            version = str(plistlib.load(stream).get("CFBundleVersion", "unknown"))
    except (OSError, plistlib.InvalidFileException):
        version = "unknown"
    backup_root = STATE_ROOT / "app-backups"
    ensure_private_dir(backup_root)
    backup = backup_root / f"YYBMacApp-{version}-before-local-sign.app"

    sign_base = [
        "/usr/bin/codesign", "--force", "--sign", "-",
        "--preserve-metadata=entitlements,flags,runtime",
    ]
    if repair_app:
        if not backup.exists():
            shutil.copytree(YYB_APP, backup, symlinks=True, copy_function=shutil.copy2)
        macho_files: list[Path] = []
        code_bundles: list[Path] = []
        for root, directories, files in os.walk(YYB_APP, followlinks=False):
            base = Path(root)
            for name in directories:
                path = base / name
                if not path.is_symlink() and path.suffix in (".app", ".framework", ".xpc"):
                    code_bundles.append(path)
            for name in files:
                path = base / name
                probe = subprocess.run(
                    ["/usr/bin/file", "-b", str(path)],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                if "Mach-O" in probe.stdout:
                    macho_files.append(path)

        for path in macho_files:
            subprocess.run(sign_base + [str(path)], check=True)
        for path in sorted(code_bundles, key=lambda item: len(item.parts), reverse=True):
            subprocess.run(sign_base + [str(path)], check=True)
        subprocess.run(sign_base + [str(YYB_APP)], check=True)
        verified = subprocess.run(
            ["/usr/bin/codesign", "--verify", "--deep", "--strict", str(YYB_APP)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if verified.returncode != 0:
            raise FixError(f"应用宝本机签名修复后仍未通过验证；原包保存在 {backup}")

    def team_identifier(path: Path) -> str:
        result = subprocess.run(
            ["/usr/bin/codesign", "-d", "--verbose=4", str(path)],
            capture_output=True,
            text=True,
            check=False,
        )
        match = re.search(r"(?m)^TeamIdentifier=(.*)$", result.stderr)
        return match.group(1).strip() if match else ""

    repair_service = (
        YYB_SERVICE.exists()
        and team_identifier(YYB_APP) != team_identifier(YYB_SERVICE)
    )
    if repair_service:
        service_backup = backup_root / f"YYBService-{version}-before-local-sign"
        if not service_backup.exists():
            shutil.copy2(YYB_SERVICE, service_backup)
            service_backup.chmod(0o600)
        entitlement = backup_root / "YYBService-local-entitlements.plist"
        with entitlement.open("wb") as stream:
            plistlib.dump(
                {"com.apple.security.cs.disable-library-validation": True},
                stream,
            )
        entitlement.chmod(0o600)
        subprocess.run(
            [
                "/usr/bin/codesign", "--force", "--sign", "-",
                "--options", "runtime", "--entitlements", str(entitlement),
                str(YYB_SERVICE),
            ],
            check=True,
        )
        service_verified = subprocess.run(
            ["/usr/bin/codesign", "--verify", "--strict", str(YYB_SERVICE)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if service_verified.returncode != 0:
            raise FixError(f"应用宝后台服务签名修复失败；原文件保存在 {service_backup}")

    if (repair_app or repair_service) and LSREGISTER.exists():
        subprocess.run(
            [str(LSREGISTER), "-f", str(YYB_APP)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    return repair_app or repair_service


def open_path(path: Path) -> None:
    if os.environ.get("SKY_YYB_TEST_HOME"):
        say(f"[test] open {path}")
        return
    subprocess.run(["open", str(path)], check=True)


def open_new_path(path: Path) -> None:
    if os.environ.get("SKY_YYB_TEST_HOME"):
        say(f"[test] open new {path}")
        return
    subprocess.run(["open", "-n", str(path)], check=True)


def open_with_yyb(path: Path) -> None:
    if os.environ.get("SKY_YYB_TEST_HOME"):
        say(f"[test] open with YYB: {path}")
        return
    subprocess.run(["open", "-a", str(YYB_APP), str(path)], check=True)


def open_yyb_package(package: str) -> None:
    """Ask YYB itself to open a package instead of invoking its helper app.

    macOS 27 can reject YYB's generated YYBPackage bundle with
    kLSNoExecutableErr even though the signed executable is present.  YYB's
    public androws route goes through the same in-app package-opening path as
    clicking the card and remains valid across generated shortcut migrations.
    """
    route = "androws://app/callAppAutoAdaptiveEnv?pkgname=" + urllib.parse.quote(
        package, safe=""
    )
    if os.environ.get("SKY_YYB_TEST_HOME"):
        say(f"[test] open YYB package: {route}")
        return
    # Refresh LaunchServices after YYB self-updates or a local signature repair,
    # then let the registered URL handler receive the route. Using `open -a`
    # can keep looking up YYB's stale pre-update designated requirement.
    if LSREGISTER.exists():
        subprocess.run(
            [str(LSREGISTER), "-f", str(YYB_APP)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    subprocess.run(["open", route], check=True)


def wait_for(path: Path, seconds: int, prompt: str) -> bool:
    say(prompt)
    if os.environ.get("SKY_YYB_TEST_HOME"):
        return path.exists()
    deadline = time.time() + seconds
    while time.time() < deadline:
        if path.exists():
            return True
        time.sleep(2)
    return False


def fetch_official_installer() -> Path:
    say("正在查询网易官方安装包……")
    request = urllib.request.Request(
        FEVER_API,
        headers={"User-Agent": "sky-yyb-mac-fix/1.0"},
    )
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            payload = json.load(response)
        data = payload["data"]
        url = data["download_url"]
        expected_md5 = data["package_md5"].lower()
        file_name = Path(data["file_name"]).name
    except (OSError, KeyError, ValueError, json.JSONDecodeError) as exc:
        raise FixError(f"无法从网易官方接口取得安装包信息: {exc}") from exc

    downloads = HOME / "Downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    target = downloads / file_name
    if target.exists() and md5(target) == expected_md5:
        say(f"已存在并通过校验：{target.name}")
        return target

    partial = target.with_suffix(target.suffix + ".download")
    say(f"正在从网易官方下载 {file_name}……")
    try:
        with urllib.request.urlopen(url, timeout=60) as response, partial.open("wb") as out:
            shutil.copyfileobj(response, out, 1024 * 1024)
    except OSError as exc:
        partial.unlink(missing_ok=True)
        raise FixError(f"下载失败: {exc}") from exc
    if md5(partial) != expected_md5:
        partial.unlink(missing_ok=True)
        raise FixError("官方安装包 MD5 校验失败，已删除临时文件。")
    os.replace(partial, target)
    return target


def md5(path: Path) -> str:
    # MD5 is used only to match the checksum published by NetEase's download
    # API, not for signatures or any other security boundary.
    digest = hashlib.md5()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_prerequisites() -> bool:
    if sys.platform != "darwin" and not os.environ.get("SKY_YYB_TEST_HOME"):
        raise FixError("此工具只支持 macOS。")
    if not YYB_APP.exists():
        raise FixError("未找到腾讯应用宝。请先从腾讯官方安装 YYBMacApp.app。")
    if not PREFIX.exists():
        open_path(YYB_APP)
        if not wait_for(PREFIX, 600, "请在应用宝中完成首次初始化，本窗口会自动等待……"):
            raise FixError("应用宝 Wine 引擎尚未初始化，请打开应用宝后重新运行本工具。")

    if find_fever_launcher() is None:
        installer = fetch_official_installer()
        open_with_yyb(installer)
        launcher = PREFIX / "drive_c/Program Files/FeverGames/FeverGamesLauncher.exe"
        if not wait_for(
            launcher,
            900,
            "已交给应用宝安装。请在出现的安装界面完成网易发烧游戏平台安装……",
        ):
            raise FixError("等待发烧游戏平台安装超时；安装完成后再次运行本工具即可续接。")

    if not SKY_EXE.exists():
        parent = shortcut_for(PACKAGE_PARENT)
        open_path(parent or YYB_APP)
        if not wait_for(
            SKY_EXE,
            3600,
            "请在发烧游戏平台内登录并安装《光·遇》；本窗口会自动等待……",
        ):
            say("尚未检测到 Sky.exe。安装完游戏后，再双击 install.command 即可自动续接。")
            return False
    return True


def upsert_reg_value(text: str, section: str, name: str, value: str) -> str:
    header_re = re.compile(rf"(?m)^\[{re.escape(section)}\](?: [0-9]+)?$")
    match = header_re.search(text)
    line = f'"{name}"={value}'
    if not match:
        suffix = "" if text.endswith("\n") else "\n"
        return text + suffix + f"\n[{section}] {int(time.time())}\n{line}\n"
    section_end = text.find("\n[", match.end())
    if section_end < 0:
        section_end = len(text)
    block = text[match.start():section_end]
    value_re = re.compile(rf'(?m)^"{re.escape(name)}"=.*$')
    if value_re.search(block):
        block = value_re.sub(lambda _match: line, block)
    else:
        block = block.rstrip("\n") + "\n" + line + "\n"
    return text[:match.start()] + block + text[section_end:]


def delete_reg_value(text: str, section: str, name: str) -> str:
    header_re = re.compile(rf"(?m)^\[{re.escape(section)}\](?: [0-9]+)?$")
    match = header_re.search(text)
    if not match:
        return text
    section_end = text.find("\n[", match.end())
    if section_end < 0:
        section_end = len(text)
    block = text[match.start():section_end]
    value_re = re.compile(rf'(?m)^"{re.escape(name)}"=.*\n?')
    block = value_re.sub("", block)
    return text[:match.start()] + block + text[section_end:]


def patch_registries(backups: BackupSet) -> list[str]:
    changed: list[str] = []
    for path in (USER_REG, SYSTEM_REG):
        if not path.exists():
            raise FixError(f"Wine 注册表不存在：{path}")
        backups.capture(path)

    user = USER_REG.read_text(encoding="utf-8")
    mac_driver = "Software\\\\Wine\\\\Mac Driver"
    user = upsert_reg_value(user, mac_driver, "RetinaMode", '"Y"')
    user = upsert_reg_value(user, mac_driver, "CursorClippingLocksWindows", '"N"')
    user = upsert_reg_value(user, mac_driver, "UseConfinementCursorClipping", '"N"')
    # Remove variables used only by the superseded M4 injection prototype.
    # The verified M2/M4 winevulkan patch needs no custom process environment.
    if detected_chip() == "Apple M4":
        user = delete_reg_value(user, "Environment", "DYLD_INSERT_LIBRARIES")
        user = delete_reg_value(user, "Environment", "SKY_YYB_GPU_COMPAT")

    layers = "Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\\\\AppCompatFlags\\\\Layers"
    executables = [
        r"C:\\FeverApps\\sky\\Sky.exe",
        r"C:\\Program Files\\FeverGames\\FeverGamesLauncher.exe",
    ]
    for version in installed_fever_versions():
        executables.extend(
            [
                rf"C:\\Program Files\\FeverGames\\{version.name}\\FeverGamesInstaller.exe",
                rf"C:\\Program Files\\FeverGames\\{version.name}\\FeverGamesWeb.exe",
            ]
        )
    for executable in executables:
        user = upsert_reg_value(user, layers, executable, '"~ HIGHDPIAWARE"')
    fever_window = "Software\\\\FeverGames\\\\FeverGamesInstaller\\\\window"
    if re.search(rf"(?m)^\[{re.escape(fever_window)}\](?: [0-9]+)?$", user):
        # Fever stores its *client* size here, but Wine adds a 19x48-point
        # outer frame. Its 1280x712 default therefore becomes 1299x760 on a
        # 1280x800 Retina panel, larger than the 1280x710 macOS visible area.
        # Keep integer Retina 2x backing and reduce only the client geometry.
        for name in ("DefaultSize", "SizeChanged"):
            user = upsert_reg_value(user, fever_window, name, '"@Size(1240 650)"')
        changed.append("网易启动器窗口缩放为 1240×650，完整容纳于内置屏幕")
    atomic_write(USER_REG, user.encode("utf-8"))
    changed.append("Wine Retina 与逐进程 High-DPI 感知")

    system = SYSTEM_REG.read_text(encoding="utf-8")
    ifeo = "Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\\\\Image File Execution Options"
    for exe_name in ("FeverGamesLauncher.exe", "FeverGamesInstaller.exe", "FeverGamesWeb.exe", "Sky.exe"):
        system = upsert_reg_value(system, ifeo + "\\\\" + exe_name, "dpiAwareness", "dword:00000002")
    atomic_write(SYSTEM_REG, system.encode("utf-8"))
    changed.append("启动器与光遇 Per-Monitor DPI 感知")
    return changed


def patch_apps_db(backups: BackupSet) -> list[str]:
    if not APPS_DB.exists():
        return []
    database = load_json(APPS_DB)
    sky_keys = []
    for key, record in database.items():
        install_path = str(record.get("install_path", "")).lower()
        if (
            key.startswith(PACKAGE_SKY_PREFIX)
            and (record.get("game_id") == "63" or install_path.endswith("feverapps\\sky"))
        ):
            sky_keys.append(key)
    if not sky_keys:
        return []
    backups.capture(APPS_DB)
    for key in sky_keys:
        record = database[key]
        record.update(
            {
                "entry_path": "fevergames://mygame/?gameId=63&autoRun=1",
                "launcher_id": "fevergames_launcher",
                "launcher_package": PACKAGE_PARENT,
                "launcher_child_exe": "sky.exe",
            }
        )
    parent = database.get(PACKAGE_PARENT)
    if parent is not None:
        parent["entry_path"] = r"C:\Program Files\FeverGames\FeverGamesLauncher.exe"
        parent["install_path"] = r"C:\Program Files\FeverGames"
    encoded = (json.dumps(database, ensure_ascii=False, indent=4) + "\n").encode("utf-8")
    atomic_write(APPS_DB, encoded)
    return [f"修复 {len(sky_keys)} 个应用宝协议启动入口"]


def patch_fever_shortcuts(backups: BackupSet) -> list[str]:
    programs = PREFIX / "drive_c/users"
    if not programs.exists():
        return []
    pattern = re.compile(
        rb"(?m)^URL=fevergames://mygame/\?gameId=63[^\r\n]*"
    )
    replacement = b"URL=fevergames://mygame/?gameId=63&autoRun=1"
    changed = 0
    # YYB rebuilds apps.db from both the Desktop and Start Menu copies. Patch
    # every matching shortcut source so the cached entry cannot regress.
    for shortcut in programs.rglob("*.url"):
        data = shortcut.read_bytes()
        if not pattern.search(data):
            continue
        updated = pattern.sub(replacement, data)
        if updated == data:
            continue
        backups.capture(shortcut)
        atomic_write(shortcut, updated)
        changed += 1
    return [f"修复 {changed} 个发烧平台自动启动入口"] if changed else []


def ensure_gpu_compat() -> bool:
    if not is_apple_m4():
        return False
    engine = find_engine_app()
    if not engine:
        raise FixError("未找到应用宝 Wine 引擎，无法安装 Apple Silicon GPU 兼容层。")
    moltenvk = engine / "Contents/Frameworks/libMoltenVK.dylib"
    if not GPU_COMPAT_SOURCE.exists():
        raise FixError(f"GPU 兼容层源码缺失：{GPU_COMPAT_SOURCE}")
    compiler = Path("/usr/bin/clang")
    if not compiler.exists():
        raise FixError("需要 Apple clang 编译 M4 兼容层；请先安装 Xcode Command Line Tools。")

    ensure_private_dir(GPU_COMPAT_DIR)
    signature = hashlib.sha256(
        GPU_COMPAT_SOURCE.read_bytes()
        + str(moltenvk.stat().st_mtime_ns).encode("ascii")
        + str(moltenvk.stat().st_size).encode("ascii")
    ).hexdigest()
    stamp = GPU_COMPAT_DIR / "build.json"
    try:
        current = json.loads(stamp.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        current = {}
    if GPU_COMPAT_DYLIB.exists() and current.get("signature") == signature:
        return True

    temporary = GPU_COMPAT_DYLIB.with_suffix(".dylib.new")
    command = [
        str(compiler),
        "-arch", "x86_64",
        "-Os",
        "-dynamiclib",
        "-Wall", "-Wextra", "-Werror",
        f"-Wl,-rpath,{moltenvk.parent}",
        "-o", str(temporary),
        str(GPU_COMPAT_SOURCE),
        str(moltenvk),
    ]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode:
        temporary.unlink(missing_ok=True)
        detail = (result.stderr or result.stdout).strip().splitlines()
        raise FixError("M4 GPU 兼容层编译失败：" + (detail[-1] if detail else "未知错误"))
    temporary.chmod(0o700)
    os.replace(temporary, GPU_COMPAT_DYLIB)
    atomic_write(
        stamp,
        (json.dumps({"signature": signature}, indent=2) + "\n").encode("utf-8"),
        0o600,
    )
    return True


def ensure_shortcut_wrapper_assets() -> None:
    for source in (SHORTCUT_WRAPPER_SOURCE, SHORTCUT_CONTROLLER_SOURCE):
        if not source.exists():
            raise FixError(f"M4 自动启动组件缺失：{source}")
    compiler = Path("/usr/bin/clang")
    if not compiler.exists():
        raise FixError("需要 Apple clang 编译 M4 自动启动组件；请先安装 Xcode Command Line Tools。")
    ensure_private_dir(GPU_COMPAT_DIR)
    signature = hashlib.sha256(
        SHORTCUT_WRAPPER_SOURCE.read_bytes() + SHORTCUT_CONTROLLER_SOURCE.read_bytes()
    ).hexdigest()
    stamp = GPU_COMPAT_DIR / "shortcut-build.json"
    try:
        current = json.loads(stamp.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        current = {}
    if not SHORTCUT_WRAPPER.exists() or current.get("signature") != signature:
        temporary = SHORTCUT_WRAPPER.with_name(SHORTCUT_WRAPPER.name + ".new")
        result = subprocess.run(
            [
                str(compiler), "-arch", "arm64", "-Os", "-Wall", "-Wextra", "-Werror",
                "-o", str(temporary), str(SHORTCUT_WRAPPER_SOURCE),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode:
            temporary.unlink(missing_ok=True)
            detail = (result.stderr or result.stdout).strip().splitlines()
            raise FixError("M4 自动启动组件编译失败：" + (detail[-1] if detail else "未知错误"))
        temporary.chmod(0o700)
        os.replace(temporary, SHORTCUT_WRAPPER)
    atomic_write(SHORTCUT_CONTROLLER, SHORTCUT_CONTROLLER_SOURCE.read_bytes(), 0o700)
    atomic_write(
        stamp,
        (json.dumps({"signature": signature}, indent=2) + "\n").encode("utf-8"),
        0o600,
    )


def sign_generated_shortcut(app: Path) -> None:
    result = subprocess.run(
        ["/usr/bin/codesign", "--force", "--deep", "--sign", "-", str(app)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode:
        detail = (result.stderr or result.stdout).strip().splitlines()
        raise FixError(f"无法更新应用宝本地快捷入口 {app.name}：" + (detail[-1] if detail else "签名失败"))


def install_shortcut_wrappers(
    backups: BackupSet, packages: list[str] | None = None
) -> list[str]:
    if not is_apple_m4():
        return []
    ensure_shortcut_wrapper_assets()
    ensure_private_dir(SHORTCUT_BACKUP_DIR)
    if packages is None:
        packages = [PACKAGE_PARENT]
        sky_package = find_sky_package()
        if sky_package:
            packages.append(sky_package)
    installed = 0
    for package in packages:
        for app in all_shortcuts_for(package):
            executable = app / "Contents/MacOS/YYBPackage"
            sibling = executable.with_name(SHORTCUT_ORIGINAL_NAME)
            if not executable.exists():
                continue
            if is_shortcut_wrapper(executable) and sibling.exists():
                external_backup = shortcut_backup(app, package)
                ensure_private_dir(external_backup.parent)
                if not external_backup.exists():
                    atomic_write(external_backup, sibling.read_bytes(), 0o700)
                installed += 1
                continue

            # These are YYB-generated, already ad-hoc-signed launch shortcuts—not
            # Tencent's signed YYB or Wine engine. Preserve the exact generated
            # executable outside the bundle so restore can always put it back.
            original = executable.read_bytes()
            external_backup = shortcut_backup(app, package)
            ensure_private_dir(external_backup.parent)
            atomic_write(external_backup, original, 0o700)
            backups.capture(executable)
            backups.capture(sibling)
            code_resources = app / "Contents/_CodeSignature/CodeResources"
            backups.capture(code_resources)
            atomic_write(sibling, original, 0o700)
            atomic_write(executable, SHORTCUT_WRAPPER.read_bytes(), 0o755)
            sign_generated_shortcut(app)
            installed += 1
    return [f"让 {installed} 个应用宝入口自动启用 M4 兼容环境"] if installed else []


def restore_shortcut_wrappers() -> int:
    restored = 0
    if not SHORTCUT_BACKUP_DIR.exists():
        return restored
    for root in shortcut_roots():
        if not root.exists():
            continue
        for app in root.glob("*.app"):
            package = shortcut_package(app)
            if not package:
                continue
            executable = app / "Contents/MacOS/YYBPackage"
            backup = shortcut_backup(app, package)
            legacy_backup = SHORTCUT_BACKUP_DIR / f"{package}.YYBPackage"
            if not backup.exists() and legacy_backup.exists():
                backup = legacy_backup
            sibling = executable.with_name(SHORTCUT_ORIGINAL_NAME)
            if not backup.exists() or not is_shortcut_wrapper(executable):
                continue
            atomic_write(executable, backup.read_bytes(), 0o755)
            sibling.unlink(missing_ok=True)
            sign_generated_shortcut(app)
            restored += 1
    return restored


def start_gpu_compat_engine() -> bool:
    if not ensure_gpu_compat():
        return False
    engine = find_engine_app()
    if not engine:
        raise FixError("应用宝 Wine 引擎不完整。")
    # Starting the engine app directly is intentionally session-only: it avoids
    # modifying or re-signing Tencent's app while ensuring its Wine children
    # inherit the compatibility library. The parent Fever shortcut can then
    # connect normally; the generated Sky child shortcut cannot initialize the
    # engine this way and otherwise stalls at 99%.
    subprocess.run(
        [
            "open", "-n",
            "--env", f"WINEPREFIX={PREFIX}",
            "--env", "SKY_YYB_GPU_COMPAT=1",
            "--env", f"DYLD_INSERT_LIBRARIES={GPU_COMPAT_DYLIB}",
            str(engine),
        ],
        check=True,
    )
    time.sleep(8)
    return True


def patch_mmkv(backups: BackupSet) -> list[str]:
    if not PUBLIC_MMKV.exists() or not PUBLIC_MMKV_CRC.exists():
        return []
    blob = bytearray(PUBLIC_MMKV.read_bytes())
    prefix = b"exe_app_retina_zoom_ratio_" + PACKAGE_PARENT.encode("ascii")
    actual_size = struct.unpack_from("<I", blob, 0)[0]
    if actual_size <= 0 or actual_size + 4 > len(blob):
        raise FixError("应用宝 MMKV 长度异常，拒绝写入。")
    meta = bytearray(PUBLIC_MMKV_CRC.read_bytes())
    if len(meta) < 40:
        raise FixError("应用宝 MMKV CRC 文件异常，拒绝写入。")
    original_crc = zlib.crc32(blob[4:4 + actual_size]) & 0xFFFFFFFF
    if struct.unpack_from("<I", meta, 0)[0] != original_crc:
        raise FixError("应用宝 MMKV 校验不一致；请完全退出应用宝后重试。")
    version = struct.unpack_from("<I", meta, 4)[0]
    if version >= 3 and struct.unpack_from("<I", meta, 28)[0] != actual_size:
        raise FixError("应用宝 MMKV 元数据长度不一致，拒绝写入。")
    if any(meta[12:28]) or (len(meta) >= 112 and any(meta[104:112])):
        raise FixError("不支持加密或带过期配置的 MMKV，拒绝写入。")
    positions = []
    start = 0
    while True:
        index = blob.find(prefix, start, actual_size + 4)
        if index < 0:
            break
        positions.append(index)
        start = index + len(prefix)
    replacements = 0
    for index in positions:
        end = index + len(prefix)
        while end < len(blob) and blob[end] not in (0x04,):
            end += 1
        if bytes(blob[end:end + 2]) == b"\x04\x03" and end + 5 <= len(blob):
            old = bytes(blob[end + 2:end + 5])
            if old in (b"1.0", b"2.0"):
                blob[end + 2:end + 5] = b"2.0"
                replacements += 1
    # A fresh YYB install has no saved zoom records. Append ordinary MMKV
    # string entries instead of silently reporting success without enabling HD.
    packages = [PACKAGE_PARENT]
    sky_package = find_sky_package()
    if sky_package:
        packages.append(sky_package)
    def varint(value: int) -> bytes:
        result = bytearray()
        while value >= 128:
            result.append((value & 127) | 128)
            value >>= 7
        result.append(value)
        return bytes(result)
    additions = bytearray()
    for package in packages:
        key = b"exe_app_retina_zoom_ratio_" + package.encode("ascii")
        # Appending also supersedes an older entry or a tombstone for this key.
        additions.extend(varint(len(key)) + key + b"\x04\x032.0")
    end = actual_size + 4
    needed = end + len(additions)
    if needed > len(blob):
        blob.extend(b"\0" * (((needed + 16383) // 16384) * 16384 - len(blob)))
    blob[end:needed] = additions
    actual_size += len(additions)
    struct.pack_into("<I", blob, 0, actual_size)
    crc = zlib.crc32(blob[4:4 + actual_size]) & 0xFFFFFFFF
    struct.pack_into("<I", meta, 0, crc)
    # Tencent MMKV v3+ stores both the current and last-confirmed sizes/CRCs.
    # Keep the full-write sequence in sync so other readers reload the file.
    struct.pack_into("<I", meta, 8, (struct.unpack_from("<I", meta, 8)[0] + 1) & 0xFFFFFFFF)
    if version >= 3:
        struct.pack_into("<III", meta, 28, actual_size, actual_size, crc)
    backups.capture(PUBLIC_MMKV)
    backups.capture(PUBLIC_MMKV_CRC)
    atomic_write(PUBLIC_MMKV, bytes(blob))
    atomic_write(PUBLIC_MMKV_CRC, bytes(meta))
    return [f"将 {len(packages)} 个启动器/游戏入口设为 Retina 2×"]


def patch_preferences(backups: BackupSet, fps: int) -> list[str]:
    if not PREFERENCES.exists():
        return []
    data = bytearray(PREFERENCES.read_bytes())
    if data[:4] != b"PREF" or len(data) < 32:
        raise FixError("光遇 preferences.sav 格式不符合预期，拒绝写入。")
    counts = struct.unpack_from("<4I", data, 8)
    record_count = sum(counts)
    string_base = struct.unpack_from("<I", data, 24)[0]
    if 28 + record_count * 8 != string_base or string_base >= len(data):
        raise FixError("光遇偏好表结构不符合预期，拒绝写入。")
    found: set[str] = set()
    for index in range(record_count):
        record = 28 + index * 8
        name_offset = struct.unpack_from("<I", data, record)[0]
        name_start = string_base + name_offset
        try:
            name_end = data.index(0, name_start)
        except ValueError as exc:
            raise FixError("光遇偏好表字符串损坏，拒绝写入。") from exc
        name = data[name_start:name_end].decode("utf-8", "replace")
        if name == "quality_fps":
            struct.pack_into("<I", data, record + 4, fps)
            found.add(name)
        elif name == "kUserPreference_MotionBlurScalar":
            struct.pack_into("<f", data, record + 4, 0.0)
            found.add(name)
        elif name == "kUserPreference_Fullscreen":
            # Use the game's saved windowed mode so YYB/Wine can expose the
            # native macOS close, minimize and fullscreen titlebar controls.
            struct.pack_into("<I", data, record + 4, 0)
            found.add(name)
    if "quality_fps" not in found:
        # The initial PREF only contains first_open_ts and readback settings.
        # Graphics preferences are created after the first successful session.
        return ["帧率/窗口设置待首次成功进入游戏后生成；退出游戏后重跑 install.command"]
    backups.capture(PREFERENCES)
    atomic_write(PREFERENCES, bytes(data))
    window_message = (
        "、默认窗口化"
        if "kUserPreference_Fullscreen" in found
        else "；窗口偏好尚未生成，请在游戏内切换一次窗口化后重跑 install.command"
    )
    return [f"光遇目标帧率 {fps} FPS、关闭动态模糊{window_message}"]


def apply_fix(fps: int) -> list[str]:
    stop_related_processes()
    backups = BackupSet()
    changes: list[str] = []
    try:
        if repair_yyb_signature():
            changes.append("修复应用宝主 App/后台服务签名与 LaunchServices 注册（原文件已备份）")
        changes.extend(patch_m2_vulkan_compat(backups))
        changes.extend(patch_registries(backups))
        changes.extend(patch_apps_db(backups))
        changes.extend(patch_fever_shortcuts(backups))
        changes.extend(patch_mmkv(backups))
        changes.extend(patch_preferences(backups, fps))
        changes.extend(install_fever_window_fix())
        if is_apple_m4() and verified_winevulkan_patch_active():
            restored = restore_shortcut_wrappers()
            if restored:
                changes.append(f"恢复 {restored} 个应用宝原始入口，使用 M2 同款正常启动链路")
            sky_package = find_sky_package()
            if detected_chip() == "Apple M4" and sky_package:
                redirected = install_shortcut_wrappers(backups, [sky_package])
                if redirected:
                    changes.append("启动台光遇图标改走已验证的网易平台登录链路")
        else:
            if ensure_gpu_compat():
                changes.append("Apple M4 Vulkan 设备兼容层")
            changes.extend(install_shortcut_wrappers(backups))
    except Exception:
        restore_from(backups.root, announce=False)
        raise
    backups.finish()
    return changes


def find_sky_package() -> str | None:
    if not APPS_DB.exists():
        return None
    for key, record in load_json(APPS_DB).items():
        if key.startswith(PACKAGE_SKY_PREFIX) and (
            record.get("game_id") == "63"
            or str(record.get("install_path", "")).lower().endswith("feverapps\\sky")
        ):
            return key
    return None


def launch() -> None:
    gpu_compat = False
    if is_apple_m4() and not verified_winevulkan_patch_active():
        gpu_compat = start_gpu_compat_engine()
    if gpu_compat:
        say("已启用 Apple M4 Vulkan 兼容启动环境。")
    package = find_sky_package()
    child = shortcut_for(package) if package else None
    parent = shortcut_for(PACKAGE_PARENT)
    if verified_winevulkan_patch_active() and parent:
        say("正在通过应用宝正常启动网易发烧游戏平台；请在平台里点“开始游戏”。")
        # YYB mirrors this bundle under /Applications with the same bundle id.
        # The public package route can cold-start both copies, leaving two Dock
        # entries and two platform windows. The repaired internal bundle is the
        # canonical entry and `open` will focus it if it is already running.
        open_path(parent)
    elif gpu_compat and parent:
        say("正在启动网易发烧游戏平台；请在平台里点“开始游戏”。")
        open_path(parent)
    elif child:
        say(f"正在启动《光·遇》：{child.name}")
        open_new_path(child)
    elif parent:
        say("未找到独立光遇快捷方式，先打开网易发烧游戏平台。")
        open_path(parent)
    else:
        say("未找到应用宝快捷方式，已打开应用宝。")
        open_path(YYB_APP)


def restore_from(root: Path, announce: bool = True) -> None:
    manifest_path = root / "manifest.json"
    if not manifest_path.exists():
        # During an interrupted apply, the in-memory manifest is not available.
        return
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    stop_related_processes()
    for item in manifest:
        target = Path(item["path"])
        if item["existed"]:
            source = root / item["backup"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            if isinstance(item.get("mode"), int):
                target.chmod(item["mode"])
        elif target.exists():
            target.unlink()
    if announce:
        say(f"已从 {root.name} 恢复。")


def restore_latest() -> None:
    if not LATEST_FILE.exists():
        raise FixError("没有找到可恢复的本地备份。")
    restore_from(Path(LATEST_FILE.read_text(encoding="utf-8").strip()))
    restored_vulkan = restore_winevulkan_originals()
    if restored_vulkan:
        say(f"已恢复 {restored_vulkan} 个 Wine Vulkan 原始组件。")
    restored = restore_shortcut_wrappers()
    if restored:
        say(f"已恢复 {restored} 个应用宝原始启动入口。")


def status() -> int:
    checks = {
        "腾讯应用宝": YYB_APP.exists(),
        "应用宝 Wine 引擎": PREFIX.exists(),
        "网易发烧游戏平台": find_fever_launcher() is not None,
        "光遇 PC 国服": SKY_EXE.exists(),
        "应用宝应用数据库": APPS_DB.exists(),
        "光遇偏好文件": PREFERENCES.exists(),
    }
    if detected_chip() in ("Apple M2", "Apple M4"):
        checks[f"{detected_chip()} Vulkan 兼容补丁"] = verified_winevulkan_patch_active()
        checks["网易启动器原生窗口坐标"] = fever_window_fix_installed()
    if is_apple_m4():
        if detected_chip() == "Apple M4":
            sky_package = find_sky_package()
            sky_entries = all_shortcuts_for(sky_package) if sky_package else []
            checks["启动台光遇入口"] = bool(sky_entries) and SHORTCUT_CONTROLLER.exists() and all(
                is_shortcut_wrapper(app / "Contents/MacOS/YYBPackage")
                and (app / "Contents/MacOS" / SHORTCUT_ORIGINAL_NAME).exists()
                for app in sky_entries
            )
        else:
            checks["Apple M4 GPU 兼容层"] = GPU_COMPAT_DYLIB.exists()
            parent_entries = all_shortcuts_for(PACKAGE_PARENT)
            checks["直接点击启动"] = bool(parent_entries) and all(
                is_shortcut_wrapper(app / "Contents/MacOS/YYBPackage")
                for app in parent_entries
            )
    for label, ok in checks.items():
        say(f"{'✓' if ok else '✗'} {label}")
    chip = detected_chip()
    if chip:
        say(f"  芯片：{chip}")
    if chip in ("Apple M2", "Apple M4"):
        engine_dll = YYB_DATA / M2_WINEVULKAN_RELATIVE
        if engine_dll.exists():
            digest = sha256(engine_dll)
            state = (
                "已应用"
                if digest in {item[1] for item in WINEVULKAN_BUILDS}
                else "原版/待应用"
                if digest in {item[0] for item in WINEVULKAN_BUILDS}
                else "版本未验证"
            )
            say(f"  {chip} Vulkan 兼容层：{state}（{digest[:16]}…）")
    if SKY_EXE.exists():
        say(f"  Sky.exe SHA-256: {sha256(SKY_EXE)[:16]}…")
    return 0 if all(checks.values()) else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="在 macOS 应用宝中安装、修复并启动光遇 PC 国服")
    parser.add_argument("command", nargs="?", choices=("setup", "fix", "launch", "status", "restore"), default="setup")
    parser.add_argument("--fps", type=int, choices=(30, 60, 120), default=60)
    args = parser.parse_args()
    try:
        if args.command == "status":
            return status()
        if args.command == "restore":
            restore_latest()
            return 0
        if args.command == "launch":
            launch()
            return 0
        if args.command == "setup" and not ensure_prerequisites():
            return 2
        changes = apply_fix(args.fps)
        say("\n修复完成：")
        for change in changes:
            say(f"  ✓ {change}")
        say("所有原文件备份仅保存在本机：" + str(STATE_ROOT / "backups"))
        launch()
        return 0
    except FixError as exc:
        print(f"错误：{exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\n已取消。重新运行会从当前阶段续接。", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
