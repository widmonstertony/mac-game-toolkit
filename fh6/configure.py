#!/usr/bin/env python3
"""Reapply the FH6 graphics preset without distributing runtimes or touching saves."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
import uuid
import xml.etree.ElementTree as ET

CONFIG_SUFFIX = Path("AppData/Local/ForzaHorizon6/LocalStorage_Shared/ForzaUserConfigSelections/UserConfigSelections")
GPU = {"D3DM_VENDOR_ID": "0x8086", "D3DM_DEVICE_ID": "0x56a1",
       "D3DM_DEVICE_DESCRIPTION": "Intel(R) Arc(TM) A750 Graphics"}
OPTIONS = {
    "UseDynamicOptimization": "0", "FrameRate": "3", "VSync": "1",
    "FSR3Mode": "2", "DLSSMode": "0", "DLSSGMode": "0", "XeSSMode": "0",
    "ResolutionScaling": "0", "TAA": "0", "DLAA": "0", "FSR3AA": "0", "XeSSAA": "0",
    "MotionBlurAmount": "0", "MotionBlurQuality": "0", "ShowFPS": "1",
    "CarLOD": "4", "EnvStreamingTex": "2", "GeometryQuality": "2",
    "ReflectionQuality": "1", "SSRQuality": "0", "RTReflectionQuality": "0",
    "ShadowQuality": "1", "NightShadows": "0", "SSGIQuality": "0", "RTGIQuality": "0",
    "ShaderQuality": "2", "ParticlesSettings": "1", "VolumetricFogQuality": "1",
}
SETTINGS = {
    "PresentInterval": "1", "UseDynamicOptimization": "0", "ShowFPS": "1",
    "FSR3Sharpness": "0.700000", "EnableWindshieldReflections": "0",
    "CollidableShadows": "0", "GlobalSpecularCubemapResolution": "128",
    "CarSpecularCubemapMSAA": "0", "MirrorResolution": "160",
    "HalfRateMirror": "1", "MirrorFarDistance": "500",
}
DYNAMIC = {"CarFocusLODMinMax": "Ultra", "CarSpecularCubemapResolution": "Low",
           "EnvMapFrequencyScale": "Low", "CarReflectionLOD": "Low"}


class ConfigError(Exception):
    pass


def digest(data):
    return hashlib.sha256(data).hexdigest()


def config_path(bottle):
    found = sorted((bottle / "drive_c/users").glob("*/" + str(CONFIG_SUFFIX)))
    if len(found) != 1:
        raise ConfigError("需要唯一的 FH6 配置文件（先启动游戏并正常退出一次）；当前找到 %d 个。" % len(found))
    path = found[0]
    if not path.resolve().is_relative_to(bottle.resolve()):
        raise ConfigError("配置文件位于 bottle 外部，拒绝修改链接目标。")
    return path


def graphics(data, resolution):
    parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
    root = ET.fromstring(data, parser=parser)
    if root.tag != "UserConfig" or root.get("Version") != "52":
        raise ConfigError("仅支持已核对的 FH6 UserConfig Version=52；未知版本不会覆盖。")
    settings, selections = root.find("settings"), root.find("selections")
    if settings is None or selections is None:
        raise ConfigError("配置缺少 settings/selections。")
    values = dict(SETTINGS, ResolutionWidth=str(resolution[0]), ResolutionHeight=str(resolution[1]))
    for tag, value in values.items():
        node = settings.find(tag)
        if node is None:
            raise ConfigError("配置缺少设置：" + tag)
        node.set("value", value)
    for tag, value in DYNAMIC.items():
        node = settings.find(tag)
        if node is None:
            raise ConfigError("配置缺少设置：" + tag)
        node.set("isDynamic", "1")
        node.set("dynamicValue", value)
        for key in ("name", "value", "min", "max"):
            node.attrib.pop(key, None)
    for key, value in OPTIONS.items():
        matches = [n for n in selections.findall("option") if n.get("id") == key]
        if len(matches) != 1:
            raise ConfigError("配置缺少或重复选项：" + key)
        matches[0].set("value", value)
    # Do not change audio, camera/FOV, texture streaming budgets, input or saves.
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8") + b"\n"


def runtime(data):
    original = json.loads(data)
    if original.get("formatVersion") != 3:
        raise ConfigError("仅支持 Highball bottle formatVersion=3。")
    if not re.fullmatch(r"x64-crossover26\.3-r20", original.get("engineID", "")):
        raise ConfigError("兼容设置仅核对过 x64-crossover26.3-r20。请先通过官方 Highball 游戏配方安装该引擎；本工具不安装/替换引擎。")
    result = copy.deepcopy(original)
    result.update(renderer="d3dmetal", rendererExplicit=True, sync="msync")
    result.setdefault("environment", {})["D3DM_MTL4"] = "0"
    result.setdefault("gameEnvironment", {}).setdefault("forza-horizon-6", {}).update(GPU)
    # Preserve other games and Steam pins. This does not enable MetalFX or LSFG.
    return (json.dumps(result, ensure_ascii=False, indent=2) + "\n").encode()


def assert_idle():
    try:
        processes = subprocess.run(["/bin/ps", "-axo", "comm="], check=True,
                                   capture_output=True, text=True).stdout.splitlines()
    except (OSError, subprocess.CalledProcessError) as exc:
        raise ConfigError("无法检查进程，未修改。请在有进程读取权限的本机终端执行。") from exc
    active = []
    for command in processes:
        name = command.strip().replace("\\", "/").split("/")[-1].lower()
        # Highball can rewrite bottle.json while open; Wine may persist XML at exit.
        if name in {"highball", "forzahorizon6.exe", "steam.exe", "steamwebhelper.exe"} or (
                "/highball/" in command.lower() and name in {"wineserver", "wine", "wine64", "wine64-preloader"}):
            active.append(name)
    if active:
        raise ConfigError("先在 Highball 停止游戏环境，再退出 Highball/Windows Steam。仍在运行：" + ", ".join(sorted(set(active))))


def atomic_write(path, data, mode):
    fd, name = tempfile.mkstemp(prefix=".fh6-preset-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(name, mode)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def backup_root(bottle):
    root = bottle / "highball/fh6-presets/backups"
    if not root.resolve().is_relative_to(bottle.resolve()):
        raise ConfigError("备份目录链接指向 bottle 外部。")
    return root


def apply_changes(bottle, changes):
    """Private, byte-exact backups; reject concurrent changes; rollback on failure."""
    changes = [(p, old, new) for p, old, new in changes if old != new]
    if not changes:
        print("配置已符合，无需修改。")
        return None
    for path, old, _ in changes:
        if path.read_bytes() != old:
            raise ConfigError("文件在检查后发生变化，未修改：" + str(path))
    base = backup_root(bottle)
    base.mkdir(parents=True, exist_ok=True, mode=0o700)
    os.chmod(base, 0o700)
    folder = base / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8])
    folder.mkdir(mode=0o700)
    entries = []
    for index, (path, old, new) in enumerate(changes):
        saved = folder / (str(index) + ".original")
        saved.write_bytes(old)
        saved.chmod(0o600)
        entries.append({"path": str(path.relative_to(bottle)), "backup": saved.name,
                        "before": digest(old), "after": digest(new), "mode": path.stat().st_mode & 0o777})
    manifest = folder / "manifest.json"
    manifest.write_text(json.dumps({"version": 1, "files": entries}, indent=2) + "\n", encoding="utf-8")
    manifest.chmod(0o600)
    written = []
    try:
        for (path, old, new), entry in zip(changes, entries):
            if path.read_bytes() != old:
                raise ConfigError("写入前检测到文件变化：" + str(path))
            atomic_write(path, new, entry["mode"])
            written.append((path, old, new, entry["mode"]))
    except Exception:
        for path, old, new, mode in reversed(written):
            if path.read_bytes() == new:
                atomic_write(path, old, mode)
        raise
    print("已配置。私人备份（不要上传 GitHub）：" + str(folder))
    return folder


def restore(bottle, folder, apply=False):
    if folder.is_symlink() or folder.resolve().parent != backup_root(bottle).resolve():
        raise ConfigError("只接受当前 bottle 的备份目录。")
    manifest_path = folder / "manifest.json"
    if manifest_path.is_symlink():
        raise ConfigError("不接受链接形式的备份清单。")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("version") != 1:
        raise ConfigError("未知备份格式。")
    allowed = {bottle / "bottle.json", config_path(bottle)}
    changes = []
    for entry in manifest["files"]:
        path = bottle / entry["path"]
        saved = folder / entry["backup"]
        if path not in allowed or saved.resolve().parent != folder.resolve() or saved.is_symlink():
            raise ConfigError("备份包含未授权的目标。")
        current, old = path.read_bytes(), saved.read_bytes()
        if digest(old) != entry["before"]:
            raise ConfigError("备份已损坏，未还原。")
        if current == old:
            continue
        if digest(current) != entry["after"]:
            raise ConfigError("应用预设后文件又被游戏/用户修改，拒绝覆盖；请手动比较私人备份。")
        changes.append((path, current, old))
    print("将还原 %d 个文件；不会修改存档。" % len(changes))
    if apply:
        assert_idle()
        apply_changes(bottle, changes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["status", "setup", "restore"])
    parser.add_argument("--highball-home", type=Path, default=Path.home() / "Library/Application Support/Highball")
    parser.add_argument("--bottle", default="Games", help="Highball 环境目录名，不是 Windows 用户名")
    parser.add_argument("--resolution", choices=["2560x1600", "1920x1200", "1920x1080", "1280x800"], default="2560x1600")
    parser.add_argument("--prepare-runtime", action="store_true", help="额外合并 r20 引擎的兼容设置；先退出整个 Highball")
    parser.add_argument("--backup", type=Path, help="restore 使用的私人备份目录")
    parser.add_argument("--apply", action="store_true", help="实际修改；不加此项仅预览")
    args = parser.parse_args()
    try:
        if Path(args.bottle).name != args.bottle or args.bottle in {".", ".."}:
            raise ConfigError("--bottle 必须是单个环境目录名。")
        home = args.highball_home.expanduser().resolve()
        bottle = home / "bottles" / args.bottle
        if not bottle.resolve().is_relative_to((home / "bottles").resolve()):
            raise ConfigError("bottle 链接指向 Highball 目录外部。")
        path = config_path(bottle)
        data = path.read_bytes()
        if args.action == "restore":
            if not args.backup:
                raise ConfigError("restore 需要 --backup 备份目录。")
            restore(bottle, args.backup.expanduser(), args.apply)
            return 0
        if args.action == "status":
            root = ET.fromstring(data)
            for tag in ("ResolutionWidth", "ResolutionHeight", "PresentInterval"):
                print(tag + "=" + str(root.find("settings/" + tag).get("value")))
            for key in ("FSR3Mode", "DLSSMode", "DLSSGMode", "TAA"):
                node = root.find("selections/option[@id='" + key + "']")
                print(key + "=" + (node.get("value") if node is not None else "missing"))
            settings = json.loads((bottle / "bottle.json").read_bytes())
            print("Highball frameGen=" + str(settings.get("frameGen", 1)) + " (1=off; >1 仍需运行时组件)")
            print("仅报告配置，不证明实际插帧/FPS。")
            return 0
        resolution = tuple(map(int, args.resolution.split("x")))
        changes = [(path, data, graphics(data, resolution))]
        if args.prepare_runtime:
            config = bottle / "bottle.json"
            if config.is_symlink():
                raise ConfigError("不修改链接形式的 bottle.json。")
            old = config.read_bytes()
            changes.append((config, old, runtime(old)))
        print("输出 %s；恢复历史 FSR3Mode=2 超分；车辆 LOD 优先，环境低档。" % args.resolution)
        print("本预设不启用插帧，不保证 60 FPS，不修复粉色碎片涂装或每秒卡顿。")
        if args.apply:
            assert_idle()
            apply_changes(bottle, changes)
        else:
            print("预览模式：没有写入。加 --apply 后应用。")
        return 0
    except (ConfigError, OSError, ValueError, ET.ParseError, KeyError, TypeError, AttributeError) as exc:
        print("未完成：" + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
