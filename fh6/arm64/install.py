#!/usr/bin/env python3
"""Install the versioned FH6 ARM64 overlay into the existing Highball bottle."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import uuid
import zipfile

HERE = Path(__file__).resolve().parent
ENGINE = "arm64-kk-wine11.18-r4"
BOTTLE = "Forza Horizon 6"
ARCHIVE = "afae75460e5353e75766ed275c35d04973c57faac268e093e81c1ee05703676e"
MODULES = ("aarch64-unix/winecoreaudio.so", "aarch64-windows/mmdevapi.dll", "i386-windows/mmdevapi.dll")
CONFIG = "AppData/Local/ForzaHorizon6/LocalStorage_Shared/ForzaUserConfigSelections/UserConfigSelections"


def replace_one(data, pattern, value):
    def replace(match):
        return match[1] + value + match[3]
    updated, count = re.subn(pattern, replace, data)
    if count != 1:
        raise ValueError(f"Expected one setting matching {pattern!r}")
    return updated


def tune_graphics(data, native):
    xml = ET.fromstring(data)
    if xml.tag != "UserConfig" or xml.get("Version") != "52":
        raise ValueError("Expected FH6 UserConfig version 52")
    options = {
        "CarLOD": "4", "EnvStreamingTex": "4", "ShaderQuality": "4",
        "UseDynamicOptimization": "0", "MotionBlurAmount": "0",
    }
    nodes = {"UseDynamicOptimization": "0"}
    if native:
        nodes.update(zip(("ResolutionWidth", "ResolutionHeight"), native))
        nodes["Fullscreen"] = "1"
    for tag, value in nodes.items():
        data = replace_one(data, rb'(<'+tag.encode()+rb'\s+value=")([^"]*)(")', value.encode())
    for tag, value in options.items():
        pattern = rb'(<option id="'+tag.encode()+rb'"\s+value=")([^"]*)(")'
        data = replace_one(data, pattern, value.encode())
    return replace_one(data, rb'(<CarFocusLODMinMax[^<>]*?\sdynamicValue=")([^"]*)(")', b"Ultra")


def register_dialog(data):
    section = rb'[Software\\Wine\\AppDefaults\\forzahorizon6.exe\\DllOverrides]'
    entry = b'"version"="native,builtin"'
    match = re.search(re.escape(section) + rb'[^\r\n]*\r?\n(?P<body>[^\[]*)', data, re.I)
    if not match:
        return data.rstrip(b"\n") + b"\n\n" + section + b"\n" + entry + b"\n"
    old = re.findall(rb'(?im)^"version"="([^"]*)"', match["body"])
    if old == [b"native,builtin"]:
        return data
    if old:
        raise ValueError("FH6 already has a different version.dll override")
    start = match.start("body")
    return data[:start] + entry + b"\n" + data[start:]


def write_atomic(path, data, mode):
    fd, name = tempfile.mkstemp(dir=path.parent, prefix=".fh6-")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.chmod(name, mode)
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="Install after previewing")
    parser.add_argument("--native", metavar="WIDTHxHEIGHT", help="Explicit display pixel size; default preserves it")
    args = parser.parse_args()
    native = None
    if args.native:
        if not re.fullmatch(r"[1-9][0-9]{2,4}x[1-9][0-9]{2,4}", args.native):
            parser.error("--native must be WIDTHxHEIGHT")
        native = args.native.split("x")

    home = Path.home() / "Library/Application Support/Highball"
    bottle = home / "bottles" / BOTTLE
    settings = bottle / "bottle.json"
    bottle_original = settings.read_bytes()
    bottle_data = json.loads(bottle_original)
    profile = bottle_data.get("gameEnvironment", {}).get("forza-horizon-6", {})
    if (bottle_data.get("engineID"), bottle_data.get("renderer"), profile.get("VKD3D_FEATURE_LEVEL")) != (
        ENGINE, "vkd3d", "12_1"
    ):
        raise ValueError("Set up the named ARM64/KosmicKrisp r4 bottle through Highball first")
    steam = bottle / "drive_c/Program Files (x86)/Steam"
    game = steam / "steamapps/common/ForzaHorizon6"
    if not (game / "forzahorizon6.exe").is_file() or not game.resolve().is_relative_to(bottle.resolve()):
        raise ValueError("Expected an existing Windows Steam game in the ARM64 bottle")
    configs = list((bottle / "drive_c/users").glob(f"*/{CONFIG}"))
    if len(configs) != 1 or not configs[0].resolve().is_relative_to(bottle.resolve()):
        raise ValueError("Run the game and quit normally once to create a local graphics config")
    if not (home / "db/games/forza-horizon-6.json").is_file():
        raise ValueError("Set up Highball's bundled game database for the CLI first")
    if not any(os.access(home / "bin" / name, os.X_OK) for name in ("highball-headless", "highball")):
        raise ValueError("Install the Highball CLI under Highball/bin first")

    engine = home / "engines" / ENGINE
    overlay = bottle / ".fh6-arm64-spatial-audio"
    ntdll = engine / "engine/lib/wine/aarch64-unix/ntdll.so"
    graphics = engine / "renderers/vkd3d/wine"
    if not ntdll.is_file() or not graphics.is_dir():
        raise ValueError("Incomplete Highball engine")
    env = bottle_data.setdefault("environment", {})
    overlay_value = f"{overlay}:{graphics}"
    if env.get("WINEDLLPATH_PREPEND") not in (None, overlay_value) or "WINEDLLPATH_PREPEND+" in env:
        raise ValueError("An existing custom renderer overlay requires manual merge")
    env["WINEDLLPATH_PREPEND"] = overlay_value
    pins = bottle_data.setdefault("pins", [])
    if not any(pin["name"].lower() == "steam" for pin in pins):
        pins.append({
            "id": str(uuid.uuid4()).upper(), "name": "Steam",
            "path": "Program Files (x86)/Steam/steam.exe", "arguments": [],
            "environment": {"WINEESYNC": "0", "WINEMSYNC": "0"},
        })

    launch = home / "fh6-arm64" / "launch"
    registry = bottle / "user.reg"
    current = {path: path.read_bytes() for path in (settings, registry, configs[0])}
    if current[settings] != bottle_original:
        raise ValueError("Bottle changed while preparing installation")
    desired = {
        configs[0]: tune_graphics(current[configs[0]], native),
        registry: register_dialog(current[registry]),
        settings: (json.dumps(bottle_data, indent=2, ensure_ascii=False) + "\n").encode(),
    }

    archive = HERE / "payload/arm64-r4.zip"
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE:
        raise ValueError("ARM64 payload checksum mismatch")
    with zipfile.ZipFile(archive) as package:
        additions = {overlay / name: (package.read(name), 0o755 if name.endswith(".so") else 0o644)
                     for name in MODULES}
        additions[game / "version.dll"] = (package.read("version.dll"), 0o644)
    additions[launch] = ((HERE / "launch.py").read_bytes(), 0o755)
    for path, (data, _) in additions.items():
        if path.is_symlink() or path.exists() and path.read_bytes() != data:
            raise ValueError(f"An existing file would be overwritten: {path}")
    link = overlay / "aarch64-unix/ntdll.so"
    if (link.exists() or link.is_symlink()) and (not link.is_symlink() or link.resolve() != ntdll.resolve()):
        raise ValueError("Existing ntdll overlay link differs")

    if args.apply:
        live = subprocess.check_output(["/bin/ps", "-axo", "comm="], text=True).splitlines()
        if any(line == str(steam / "steam.exe") or line.lower().endswith("forzahorizon6.exe") for line in live):
            raise ValueError("Exit FH6 and its Windows Steam before applying")
        (home / "backups").mkdir(exist_ok=True)
        backup = Path(tempfile.mkdtemp(prefix="fh6-arm64-", dir=home / "backups"))
        for path in current:
            shutil.copy2(path, backup / path.name)
        created = []
        changed = []
        try:
            for path, (data, mode) in additions.items():
                if not path.exists():
                    path.parent.mkdir(parents=True, exist_ok=True)
                    write_atomic(path, data, mode)
                    created.append(path)
            if not link.is_symlink():
                link.symlink_to(ntdll)
                created.append(link)
            for path, data in desired.items():
                if path.read_bytes() != current[path]:
                    raise ValueError(f"File changed during installation: {path}")
                if data != current[path]:
                    mode = stat.S_IMODE(path.stat().st_mode)
                    write_atomic(path, data, mode)
                    changed.append((path, mode))
        except BaseException:
            for path, mode in reversed(changed):
                write_atomic(path, current[path], mode)
            for path in reversed(created):
                path.unlink(missing_ok=True)
            raise
        print(f"Installed. Local configuration backup: {backup}")
    else:
        print("Preview only. Use --apply to install.")
    print(f'Paste into the native Steam launch options for FH6 only:\n"{launch}" %command%')


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, ET.ParseError) as error:
        raise SystemExit(str(error))
