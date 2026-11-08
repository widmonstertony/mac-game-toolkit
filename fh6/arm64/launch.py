#!/usr/bin/python3
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

APP_ID = "2483190"
APP = "Forza Horizon 6"
root = Path.home() / "Library/Application Support/Highball"
bottle = root / "bottles" / APP
steam = bottle / "drive_c/Program Files (x86)/Steam"
events_path = steam / "logs/gameprocess_log.txt"
runner = next((root / "bin" / name for name in ("highball-headless", "highball")
               if os.access(root / "bin" / name, os.X_OK)), root / "bin/highball")
support = Path(__file__).resolve().parent
event_pattern = re.compile(r"AppID 2483190 (adding PID|no longer tracking PID) (\d+)")

def restore_visuals(configs):
    if len(configs) != 1:
        raise ValueError("Expected exactly one FH6 graphics configuration")
    config = configs[0]
    original = config.read_bytes()
    document = ET.fromstring(original)
    if document.tag != "UserConfig" or document.get("Version") != "52":
        raise ValueError("Unexpected FH6 graphics configuration version")
    # Restore the previous scene quality, leaving the existing display,
    # upscaling, frame-generation and audio choices in place.
    options = {
        "GeometryQuality": 5, "SSRQuality": 5, "ShadowQuality": 4,
        "NightShadows": 2, "SSGIQuality": 2, "ParticlesSettings": 4,
        "VolumetricFogQuality": 5, "LensEffects": 4, "DeformableSnowQuality": 4,
    }
    updated = original
    for key, value in options.items():
        prefix = rb'<option id="' + key.encode() + rb'"\s+value="'
        updated, count = re.subn(rb'(' + prefix + rb')[^"]*',
                                 lambda match: match[1] + str(value).encode(), updated)
        if count != 1:
            raise ValueError(f"Expected one FH6 graphics setting named {key}")
    if updated == original:
        return
    with (support / f"fh6-visual-settings-before-{time.time_ns()}.xml").open("xb") as backup:
        backup.write(original)
    with tempfile.NamedTemporaryFile(dir=config.parent, prefix=".fh6-visual-", delete=False) as stream:
        pending = Path(stream.name)
        stream.write(updated)
    try:
        pending.chmod(config.stat().st_mode & 0o777)
        if config.read_bytes() != original:
            raise RuntimeError("FH6 graphics configuration changed during launch")
        os.replace(pending, config)
    finally:
        pending.unlink(missing_ok=True)

def events(file, active):
    for line in file:
        match = event_pattern.search(line)
        if not match:
            continue
        kind, pid = match.groups()
        if kind == "adding PID" and "forzahorizon6.exe" in line.lower():
            active.add(pid)
        elif kind == "no longer tracking PID":
            active.discard(pid)

def launch(log):
    declared_app = os.environ.get("STEAM_COMPAT_APP_ID")
    if declared_app in (None, "", "0"):
        declared_app = os.environ.get("SteamAppId")
    if declared_app not in (None, "", "0", APP_ID):
        raise RuntimeError("This launch option is specific to Forza Horizon 6")
    settings = json.loads((bottle / "bottle.json").read_text())
    game = settings.get("gameEnvironment", {}).get("forza-horizon-6", {})
    if (settings.get("engineID"), settings.get("renderer"), game.get("VKD3D_FEATURE_LEVEL")) != (
        "arm64-kk-wine11.18-r4", "vkd3d", "12_1"
    ):
        raise RuntimeError("The expected ARM64/FEX + KosmicKrisp configuration is unavailable")
    if not (steam / "steamapps/common/ForzaHorizon6/forzahorizon6.exe").is_file():
        raise RuntimeError("Forza Horizon 6 is missing from the existing ARM64 Steam library")
    if not (root / "db/games/forza-horizon-6.json").is_file():
        raise RuntimeError("Highball's Forza Horizon 6 recipe is unavailable")
    allowed = ("HOME", "USER", "LOGNAME", "TMPDIR", "LANG", "LC_ALL", "LC_CTYPE", "SHELL")
    env = {key: os.environ[key] for key in allowed if key in os.environ}
    env["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
    env["HIGHBALL_HOME"] = str(root)
    # macOS Steam starts this wrapper in the game folder. After resolving
    # the shared library symlink, Highball sees that directory inside the
    # bottle and would count this Python process as a running Steam game.
    os.chdir(support)
    renderer = ["--renderer", "vkd3d"] if runner.name == "highball-headless" else []
    with events_path.open(encoding="utf-8", errors="replace") as file:
        file.seek(0, os.SEEK_END)
        visual_request = support / "restore-visuals-next-run"
        if visual_request.is_file():
            configs = list((bottle / "drive_c/users").glob(
                "*/AppData/Local/ForzaHorizon6/LocalStorage_Shared/ForzaUserConfigSelections/UserConfigSelections"
            ))
            try:
                restore_visuals(configs)
                visual_request.unlink()
                log.write("Restored FH6 scene detail\n")
            except (OSError, RuntimeError, ValueError, ET.ParseError) as error:
                log.write(f"Could not restore optional FH6 visual settings: {error}\n")
        request = support / "diagnose-next-run"
        diagnostics = support / "diagnostics.py"
        if request.is_file() and diagnostics.is_file():
            try:
                capture = support / f"fh6-session-{time.time_ns()}.log"
                subprocess.Popen([sys.executable, str(diagnostics), str(bottle), str(capture)],
                                 cwd=support, stdin=subprocess.DEVNULL, stdout=log, stderr=log)
                request.unlink()
                log.write(f"Collecting one launch in {capture.name}\n")
            except OSError as error:
                log.write(f"Could not arm optional FH6 capture: {error}\n")
        if (support / "native-steam-enabled").is_file():
            import native_steam

            args = sys.argv[1:]
            if (len(args) < 3 or args[1] != "waitforexitandrun" or
                    Path(args[2]).resolve() != (steam / "steamapps/common/ForzaHorizon6/forzahorizon6.exe").resolve()):
                raise RuntimeError("Expected the original Steam Play command for FH6")
            if declared_app != APP_ID or (os.environ.get("SteamGameId") or declared_app) != APP_ID:
                raise RuntimeError("Start Forza Horizon 6 from its original macOS Steam Play entry")
            engine = root / "engines" / settings["engineID"]
            return native_steam.run(runner, root, bottle, steam, support, engine, log, declared_app, args[3:])
        log.write("Dispatching Steam AppID 2483190 to the existing ARM64 container\n")
        child = subprocess.Popen(
            [str(runner), "run", APP, "Steam", *renderer, "--", "-silent", "-applaunch", APP_ID],
            cwd=root, env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=log,
            start_new_session=True,
        )
        active = set()
        started = False
        deadline = time.monotonic() + 150
        while True:
            if events_path.stat().st_size < file.tell():
                file.seek(0)
            events(file, active)
            if active and not started:
                started = True
                log.write("Windows Steam registered the Forza Horizon 6 process\n")
            if started and not active:
                log.write("Windows Steam recorded the game exit\n")
                return 0
            if not started and (time.monotonic() >= deadline or child.poll() not in (None, 0)):
                raise RuntimeError("Windows Steam did not start Forza Horizon 6; see the Highball launch log")
            time.sleep(0.5)

with (support / "arm64-dispatch.lock").open("w") as lock, (
    support / "arm64-dispatch.log"
).open("a", buffering=1) as log:
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise SystemExit("The existing Forza Horizon 6 launch is still running")
    try:
        sys.exit(launch(log))
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        log.write(f"{error}\n")
        raise SystemExit(str(error))
