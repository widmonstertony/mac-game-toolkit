#!/usr/bin/env python3
"""Install an already verified, local FH6 Metal FSR build in its ARM64 bottle."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

APP = "Forza Horizon 6"
ENGINE = "arm64-kk-wine11.18-r4"
GAME = "drive_c/Program Files (x86)/Steam/steamapps/common/ForzaHorizon6"
PROVIDER = "amd_fidelityfx_upscaler_dx12.dll"
ORIGINAL = "amd_fidelityfx_upscaler_original_dx12.dll"
BRIDGE = "fh6-fsr-bridge.so"


def replace_file(path, contents):
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".fh6-metal-", delete=False) as file:
        pending = Path(file.name)
        file.write(contents)
    try:
        if path.exists():
            shutil.copymode(path, pending)
        os.replace(pending, path)
    finally:
        pending.unlink(missing_ok=True)


def ensure_idle():
    processes = subprocess.check_output(["/bin/ps", "-axo", "comm"], text=True)
    if re.search(r"(?:forzahorizon6|steam)\.exe(?:$|\s)", processes, re.IGNORECASE | re.MULTILINE):
        raise RuntimeError("Exit Forza Horizon 6 and Windows Steam before changing the graphics libraries")


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--artifacts", type=Path, help="directory with verified DLLs, bridge and Metal dylib")
    args.add_argument("--rollback", action="store_true")
    options = args.parse_args()
    if options.rollback == bool(options.artifacts):
        args.error("specify exactly one of --artifacts or --rollback")

    support = Path.home() / "Library/Application Support"
    bottle = support / "Highball/bottles" / APP
    settings_path = bottle / "bottle.json"
    installed = bottle / ".fh6-metal-fsr4"
    settings = json.loads(settings_path.read_text())
    if settings["engineID"] != ENGINE or settings["renderer"] != "vkd3d":
        raise RuntimeError("The expected Highball ARM64/FEX + KosmicKrisp bottle is missing")
    sdk = bottle / GAME / "OptiScaler-FH6-SDK"
    if not (sdk.parent / "forzahorizon6.exe").is_file() or not (sdk / PROVIDER).is_file():
        raise RuntimeError("The existing Forza installation and its FidelityFX provider are required")
    ensure_idle()

    if options.rollback:
        original = sdk / ORIGINAL
        old_env = json.loads((installed / "previous-environment.json").read_text())
        if not original.is_file():
            raise RuntimeError("Original FidelityFX provider is missing")
        replace_file(sdk / PROVIDER, original.read_bytes())
        for key, value in old_env.items():
            if value is None:
                settings["environment"].pop(key, None)
            else:
                settings["environment"][key] = value
        replace_file(settings_path, (json.dumps(settings, indent=2) + "\n").encode())
        print("Restored the original FidelityFX provider and renderer environment")
        return

    src = options.artifacts.resolve(strict=True)
    for artifact in ("d3d12.dll", "d3d12core.dll", PROVIDER, BRIDGE, "libfsr4-arm64.dylib"):
        if not (src / artifact).is_file():
            raise RuntimeError(f"Missing verified artifact {artifact}")
    root = support / "protonish/fsr4-metalfx-v3"
    if not (root / "build").is_dir() or not (root / "src/fsr412_live.mm").is_file():
        raise RuntimeError("The existing native Metal FSR runtime source and cache are required")
    env = settings["environment"]
    if not env.get("WINEDLLPATH_PREPEND"):
        raise RuntimeError("The existing graphics and audio DLL search paths are required")
    changes = {
        "WINEDLLPATH_PREPEND": f"{installed / 'wine'}:{env['WINEDLLPATH_PREPEND']}",
        "FH6_NATIVE_FSR4_LIBRARY": str(installed / "libfsr4-arm64.dylib"),
        "FSR4_ROOT": str(root),
        "FSR4_BUILD_DIR": str(installed / "fsr-build"),
        "FSR4_SETTINGS": "none",
        "FSR4_PROFILE": "int8-lite",
        # The verified cache is shipped alongside the runtime. Its compiler
        # probes Xcode even on a cache hit when runtime compilation is enabled.
        "FSR4_COMPILE": "0",
        "FSR4_HOOK_LOG": str(installed / "fsr4.log"),
        "VKD3D_SHADER_CACHE_PATH": str(installed / "vkd3d-cache"),
    }
    if any(key in settings.get("gameEnvironment", {}).get("forza-horizon-6", {}) for key in changes):
        raise RuntimeError("A per-game environment entry would override the Metal FSR installation")
    if installed.exists() or (sdk / ORIGINAL).exists() or (sdk / BRIDGE).exists():
        raise RuntimeError("Metal FSR files already exist. Inspect the previous installation before replacing them")
    (installed / "wine/aarch64-windows").mkdir(parents=True)
    (installed / "vkd3d-cache").mkdir()
    (installed / "previous-environment.json").write_text(json.dumps({key: env.get(key) for key in changes}))
    shutil.copytree(root / "build", installed / "fsr-build")
    for name in ("d3d12.dll", "d3d12core.dll"):
        shutil.copy2(src / name, installed / "wine/aarch64-windows" / name)
    shutil.copy2(src / "libfsr4-arm64.dylib", installed / "libfsr4-arm64.dylib")
    shutil.copy2(sdk / PROVIDER, sdk / ORIGINAL)
    shutil.copy2(src / BRIDGE, sdk / BRIDGE)
    env.update(changes)
    replace_file(settings_path, (json.dumps(settings, indent=2) + "\n").encode())
    try:
        replace_file(sdk / PROVIDER, (src / PROVIDER).read_bytes())
    except BaseException:
        for key, value in json.loads((installed / "previous-environment.json").read_text()).items():
            if value is None:
                env.pop(key, None)
            else:
                env[key] = value
        replace_file(settings_path, (json.dumps(settings, indent=2) + "\n").encode())
        raise
    print(f"Installed Metal FSR 4 bridge in {bottle}")


if __name__ == "__main__":
    main()
