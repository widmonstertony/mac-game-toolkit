"""Run Proton-ish's Steam bridge in the existing FH6 Highball bottle."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

BRIDGE_FILES = {
    "steam.exe": "steam.exe",
    "steamclient.dll": "steamclient.dll",
    "steamclient64.dll": "local/steamclient64.dll",
    "tier0_s.dll": "tier0_s.dll",
    "tier0_s64.dll": "tier0_s64.dll",
    "vstdlib_s.dll": "vstdlib_s.dll",
    "vstdlib_s64.dll": "vstdlib_s64.dll",
    "lsteamclient.dll": "x86_64-windows/lsteamclient.dll",
    "lsteamclient.so": "aarch64-unix/lsteamclient.so",
    "system32/lsteamclient.dll": "x86_64-windows/lsteamclient.dll",
}
LIBRARY_CONFIG = "steamapps/libraryfolders.vdf"


def runtime_path(steam, name):
    if name == "system32/lsteamclient.dll":
        # Wineboot places this trigger in a new prefix. This bottle already
        # exists, so stage it explicitly with the matching Unix overlay.
        return steam.parents[1] / "windows" / name
    return steam / name


def replace_file(source, destination):
    fd, name = tempfile.mkstemp(prefix=".fh6-steam-", dir=destination.parent)
    os.close(fd)
    pending = Path(name)
    try:
        shutil.copy2(source, pending)
        os.replace(pending, destination)
    finally:
        pending.unlink(missing_ok=True)


def apply_fsr31_profile(game, support, log):
    request = support / "fsr31-next-run"
    if not request.is_file():
        return
    provider = game / "amd_fidelityfx_upscaler_dx12.dll"
    if not provider.is_file():
        raise RuntimeError("The game's original FidelityFX upscaler is missing")
    config = game / "OptiScaler.ini"
    old = config.read_bytes()
    text = old.decode("utf-8")
    updates = {
        "Upscalers": {"Dx12Upscaler": "fsr31"},
        "FrameGen": {
            "Enabled": "true", "FGInput": "dlssg", "FGOutput": "fsrfg",
            "DrawUIOverFG": "false", "UIPremultipliedAlpha": "true",
            "DisableHudless": "false", "DisableUI": "false",
        },
        "FSRFG": {"AllowAsync": "false", "FramePacingTuning": "true"},
        "FSR": {"Fsr4Update": "false"},
        "Libraries": {"FfxDx12SRPath": r"C:\Program Files (x86)\Steam\steamapps\common\ForzaHorizon6"},
    }
    for section, values in updates.items():
        match = re.search(rf"(?m)^\[{section}\]\r?\n[^\[]*", text)
        if not match:
            raise RuntimeError(f"Missing OptiScaler section {section}")
        block = match[0]
        for key, value in values.items():
            block, count = re.subn(rf"(?m)^({key}[ \t]*=[ \t]*)[^\r\n]*",
                                   lambda line: line[1] + value, block)
            if count != 1:
                raise RuntimeError(f"Expected exactly one OptiScaler setting {section}.{key}")
        text = text[:match.start()] + block + text[match.end():]

    with tempfile.TemporaryDirectory(dir=support) as staging:
        pending = Path(staging) / config.name
        pending.write_bytes(text.encode("utf-8"))
        shutil.copymode(config, pending)
        if config.read_bytes() != old:
            raise RuntimeError("OptiScaler settings changed while preparing the FSR profile")
        backup = Path(tempfile.mkdtemp(prefix="before-fsr31-", dir=support)) / config.name
        replace_file(config, backup)
        replace_file(pending, config)
    request.unlink()
    log.write("Selected the original game FidelityFX upscaler and preserved 2x frame generation\n")


def ensure_bottle_idle(steam):
    processes = subprocess.check_output(["/bin/ps", "-axo", "comm"], text=True)
    for line in processes.splitlines():
        command = line.strip().casefold()
        if command == str(steam / "steam.exe").casefold() or command.endswith(
            (r"\forzahorizon6.exe", "/forzahorizon6.exe")
        ):
            raise RuntimeError("Forza Horizon 6 or the bottle's Steam process is still running")


def recover(steam, backup):
    manifest = backup / "originals.json"
    if not manifest.exists():
        return
    ensure_bottle_idle(steam)
    originals = json.loads(manifest.read_text())
    if set(originals) != {*BRIDGE_FILES, LIBRARY_CONFIG} or not all(
        isinstance(present, bool) for present in originals.values()
    ):
        raise RuntimeError("Invalid FH6 Steam runtime recovery manifest")
    for name, present in originals.items():
        target = runtime_path(steam, name)
        if present:
            replace_file(backup / name, target)
        else:
            target.unlink(missing_ok=True)
    manifest.unlink()


def stage(steam, bridge, backup):
    ensure_bottle_idle(steam)
    if not (steam / "steam.exe").is_file():
        raise RuntimeError("The existing FH6 Steam runtime is missing")
    for name in BRIDGE_FILES.values():
        if not (bridge / name).is_file():
            raise RuntimeError(f"Missing installed Proton-ish bridge file: {name}")
    originals = {}
    for name in (*BRIDGE_FILES, LIBRARY_CONFIG):
        target = runtime_path(steam, name)
        if target.is_symlink() or (target.exists() and not target.is_file()):
            raise RuntimeError(f"Unexpected Steam runtime file: {name}")
        originals[name] = target.exists()
        if originals[name]:
            copy = backup / name
            copy.parent.mkdir(parents=True, exist_ok=True)
            replace_file(target, copy)
    pending = backup / "originals.pending"
    pending.write_text(json.dumps(originals))
    os.replace(pending, backup / "originals.json")
    for name, source in BRIDGE_FILES.items():
        replace_file(bridge / source, runtime_path(steam, name))


def runtime_environment(runner, root, bottle, bridge, engine, client, steam):
    listing = subprocess.check_output(
        [str(runner), "env", bottle.name, "--renderer", "vkd3d"],
        env={"HOME": str(Path.home()), "HIGHBALL_HOME": str(root), "PATH": "/usr/bin:/bin:/usr/sbin:/sbin"},
        text=True, timeout=15,
    )
    env = os.environ.copy()
    for line in listing.splitlines():
        match = re.fullmatch(r'export ([A-Z][A-Z0-9_]*)="(.*)"', line)
        if not match:
            raise RuntimeError("Unexpected Highball environment output")
        key, value = match.groups()
        # Highball wraps raw values, including embedded DXVK quotes and
        # Windows backslashes. Strip only the outer delimiters.
        env[key] = value
    if env.get("WINEPREFIX") != str(bottle):
        raise RuntimeError("Highball selected a different container")
    env["PATH"] = f"{engine / 'engine/bin'}:/usr/bin:/bin:/usr/sbin:/sbin"
    env["WINEDLLPATH_PREPEND"] = f"{bridge / 'wine-overlay'}:{env['WINEDLLPATH_PREPEND']}"
    env["WINEDLLPATH"] = str(steam)
    env["WINEDLLOVERRIDES"] += ";steamclient,steamclient64=n;lsteamclient=b"
    env["STEAM_COMPAT_CLIENT_INSTALL_PATH"] = str(client)
    return env


def run(runner, root, bottle, steam, support, engine, log, app_id, game_args):
    client = Path(os.environ.get("STEAM_COMPAT_CLIENT_INSTALL_PATH") or
                  Path.home() / "Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS")
    if not (client / "steamclient.dylib").is_file():
        raise RuntimeError("The original macOS Steam client is unavailable")
    bridge = support / "native-bridge"
    backup = bottle / ".fh6-native-steam-originals"
    wine = engine / "engine/bin/wine"
    game = steam / "steamapps/common/ForzaHorizon6"
    if not (bridge / "verify-steam.exe").is_file():
        raise RuntimeError("Native Steam bridge verification program is missing")
    env = runtime_environment(runner, root, bottle, bridge, engine, client, steam)
    # Match Proton-ish's normalization for waitforexitandrun. The caller
    # validates the original Steam command and app ID before reaching here.
    for name in ("STEAM_COMPAT_APP_ID", "SteamAppId", "SteamGameId"):
        if env.get(name) in (None, "", "0"):
            env[name] = app_id
    recover(steam, backup)
    ensure_bottle_idle(steam)
    apply_fsr31_profile(game, support, log)
    try:
        stage(steam, bridge, backup)
        steam_dll = r"C:\Program Files (x86)\Steam\steamclient64.dll"
        subprocess.run([str(wine), str(bridge / "verify-steam.exe"), steam_dll],
                       env=env, cwd=support, stdout=log, stderr=log, check=True, timeout=30)
        log.write("Verified online macOS Steam session from the FH6 ARM64 bottle\n")
        result = subprocess.run(
            [str(wine), str(steam / "steam.exe"), str(game / "forzahorizon6.exe"), *game_args],
            env=env, cwd=game, stdin=subprocess.DEVNULL, stdout=log, stderr=log,
        )
        if result.returncode:
            log.write(f"Proton-ish Steam shim exited with status {result.returncode}\n")
        return result.returncode
    finally:
        recover(steam, backup)
