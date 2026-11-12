#!/usr/bin/env python3
"""Install only FH6's verified Wine 11.18 network DLLs; preserve the shared engine."""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
ARCHIVE_SHA = "57d601f7502e55abd8615f18af907fe9f82d34ad5a89633bd7fff322c551e8db"
MODULES = {
    "aarch64-windows/winhttp.dll": "a7f77c1b8904de3fedaa7b4e24a093fa4232340b52febb87b006761c3758eaf3",
    "aarch64-windows/ws2_32.dll": "619822adf30efa8affd9946a542facbff029c9042112eb3f678b5a3096cb00ef",
}
ENGINE_FILES = {
    "aarch64-unix/ntdll.so": "f23bb1bdde8996f72b3be5524865f522b0b0e20d846d19dcd17ad63000742049",
    "aarch64-unix/ws2_32.so": "9999eabc8e10c855a4dcadd4fa20b98d348680320a205482cf2cee334ef9776d",
    "aarch64-windows/winhttp.dll": "b6da3884252ba6001fea5e4699e4b86f181bf9fd59ab4bcebd2cca7c6e44db5a",
    "aarch64-windows/ws2_32.dll": "cb0075d93094509af3f2620b690cb0a06bb074892433911f994d7316bffcb5e0",
}
ANCHOR = '    env["WINEDLLPATH_PREPEND"] = f"{bridge / \'wine-overlay\'}:{env[\'WINEDLLPATH_PREPEND\']}"\n'
ADDITION = '''    # FH6-only network modules, without changing the bottle's renderer/audio paths.
    network = bottle / ".fh6-network"
    if (network / "installed.json").is_file():
        env["WINEDLLPATH_PREPEND"] = f"{network}:{env['WINEDLLPATH_PREPEND']}"
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def patched_helper(text):
    if text.count(ANCHOR) != 1:
        raise ValueError("Unknown native Steam bridge; no files changed")
    if ADDITION in text:
        return text
    if '.fh6-network' in text:
        raise ValueError("Existing network integration differs; no files changed")
    return text.replace(ANCHOR, ANCHOR + ADDITION)


def atomic_write(path, data, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(dir=path.parent, prefix=".network-")
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
        os.chmod(name, mode)
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def payload():
    archive = HERE / "payload.zip"
    if digest(archive.read_bytes()) != ARCHIVE_SHA:
        raise ValueError("Network payload archive checksum mismatch")
    with zipfile.ZipFile(archive) as bundle:
        if set(bundle.namelist()) != set(MODULES):
            raise ValueError("Unexpected network payload contents")
        files = {name: bundle.read(name) for name in MODULES}
    if any(digest(data) != MODULES[name] for name, data in files.items()):
        raise ValueError("Network DLL checksum mismatch")
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="Install after preview")
    parser.add_argument("--disable", action="store_true", help="Disable for the next launch, retaining files")
    args = parser.parse_args()
    home = Path.home() / "Library/Application Support/Highball"
    bottle = home / "bottles/Forza Horizon 6"
    engine = home / "engines/arm64-kk-wine11.18-r4/engine/lib/wine"
    support = Path.home() / "Library/Application Support/protonish/launchers/2483190"
    helper = support / "native_steam.py"
    overlay = bottle / ".fh6-network"
    marker = overlay / "installed.json"
    original = helper.read_bytes()
    desired = patched_helper(original.decode()).encode()
    if not (support / "native-steam-enabled").is_file():
        raise ValueError("FH6's native Steam route is not enabled")
    config = bottle / "bottle.json"
    settings = json.loads(config.read_bytes())
    if settings.get("engineID") != "arm64-kk-wine11.18-r4" or settings.get("renderer") != "vkd3d":
        raise ValueError("The expected FH6 ARM64/KosmicKrisp bottle is unavailable")
    for name, expected in ENGINE_FILES.items():
        if digest((engine / name).read_bytes()) != expected:
            raise ValueError(f"Unsupported shared Wine module: {name}")
    files = payload()
    # This overlay would shadow our port. An unknown local patch needs a review.
    old_winsock = bottle / ".fh6-arm64-spatial-audio/aarch64-windows/ws2_32.dll"
    if old_winsock.is_file() and digest(old_winsock.read_bytes()) != "1fb09adea5ff3fa0c2297f664dc805688030b4f3c5b9ba7a8bd416e150b33e3f":
        raise ValueError("Unknown existing FH6 Winsock overlay")
    protected = [config] + [engine / name for name in ENGINE_FILES]
    protected += list((bottle / ".fh6-arm64-spatial-audio").glob("*/*"))
    protected += list((bottle / ".fh6-metal-fsr4/wine").glob("*/*"))
    protected += list((bottle / "drive_c/users").glob(
        "*/AppData/Local/ForzaHorizon6/LocalStorage_Shared/ForzaUserConfigSelections/UserConfigSelections"))
    game = bottle / "drive_c/Program Files (x86)/Steam/steamapps/common/ForzaHorizon6"
    protected += list(game.glob("OptiScaler*.ini"))
    before = {p: digest(p.read_bytes()) for p in protected if p.is_file()}
    print("FH6 only:", "disable network overlay" if args.disable else "enable WebSocket + dual-stack UDP overlay")
    print("Native Steam, bottle settings, graphics, frame generation, audio and original modules are preserved.")
    if not args.apply:
        print("Preview only; use --apply.")
        return
    processes = subprocess.check_output(["/bin/ps", "-axo", "comm="], text=True)
    if any(line.lower().endswith("forzahorizon6.exe") for line in processes.splitlines()):
        raise ValueError("Exit FH6 before replacing its network modules")
    if args.disable:
        if marker.is_file():
            os.replace(marker, overlay / "installed.disabled.json")
        print("Disabled for the next FH6 launch.")
        return
    if helper.read_bytes() != original:
        raise ValueError("Native Steam helper changed during preparation")
    for name in MODULES:
        target = overlay / name
        if target.is_symlink() or target.exists() and digest(target.read_bytes()) != MODULES[name]:
            raise ValueError(f"Unknown existing network component: {target}")
    for name in ("ntdll.so", "ws2_32.so"):
        link = overlay / "aarch64-unix" / name
        if (link.exists() or link.is_symlink()) and (not link.is_symlink() or link.resolve() != (engine / "aarch64-unix" / name).resolve()):
            raise ValueError(f"Unknown Unix module link: {link}")
    record = support / "network-install-records" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    record.mkdir(parents=True, mode=0o700)
    atomic_write(record / "native_steam.py", original, 0o600)
    previous_marker = marker.read_bytes() if marker.exists() else None
    try:
        for name, data in files.items():
            atomic_write(overlay / name, data)
        for name in ("ntdll.so", "ws2_32.so"):
            link = overlay / "aarch64-unix" / name
            link.parent.mkdir(exist_ok=True)
            if not link.is_symlink():
                link.symlink_to(engine / "aarch64-unix" / name)
        atomic_write(helper, desired, helper.stat().st_mode & 0o777)
        changed = [str(p) for p, sha in before.items() if digest(p.read_bytes()) != sha]
        if changed:
            raise ValueError(f"Protected settings/modules changed: {changed}")
        manifest = {
            "version": 1, "app_id": "2483190", "engine": "arm64-kk-wine11.18-r4",
            "upstream_commit": "37158bb6f55e26554955fb446221687959d1fa56",
            "payload_sha256": ARCHIVE_SHA, "modules": MODULES,
            "protected_files_verified": len(before),
        }
        atomic_write(marker, json.dumps(manifest, indent=2).encode())
        atomic_write(record / "installed.json", json.dumps(manifest, indent=2).encode(), 0o600)
    except BaseException:
        atomic_write(helper, original, helper.stat().st_mode & 0o777)
        if previous_marker is not None:
            atomic_write(marker, previous_marker)
        elif marker.exists():
            marker.unlink()
        raise
    print(f"Installed; {len(before)} existing configuration/module checksums unchanged.")
    print("Start FH6 from native Steam and test joining Horizon Play; no game was started here.")


if __name__ == "__main__":
    main()
