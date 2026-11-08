#!/usr/bin/python3
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

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
    declared_app = os.environ.get("STEAM_COMPAT_APP_ID") or os.environ.get("SteamAppId")
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
    with events_path.open(encoding="utf-8", errors="replace") as file:
        file.seek(0, os.SEEK_END)
        log.write("Dispatching Steam AppID 2483190 to the existing ARM64 container\n")
        child = subprocess.Popen(
            [str(runner), "run", APP, "Steam", "--", "-silent", "-applaunch", APP_ID],
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
    except (OSError, RuntimeError, ValueError) as error:
        log.write(f"{error}\n")
        raise SystemExit(str(error))
