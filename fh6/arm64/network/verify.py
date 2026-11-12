#!/usr/bin/env python3
"""Run a loopback regression in FH6's existing runtime, without starting the game."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import threading

from loopback import Peer, Server


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--overlay", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--trace", action="store_true", help="Local probe only: record module/HTTP traces")
    parser.add_argument("--capture-wait", action="store_true", help="Sample this probe if it exceeds eight seconds")
    args = parser.parse_args()
    home = Path.home() / "Library/Application Support/Highball"
    support = Path.home() / "Library/Application Support/protonish/launchers/2483190"
    bottle = home / "bottles/Forza Horizon 6"
    engine = home / "engines/arm64-kk-wine11.18-r4"
    processes = subprocess.check_output(["/bin/ps", "-axo", "comm="], text=True)
    if any(line.lower().endswith("forzahorizon6.exe") for line in processes.splitlines()):
        sys.exit("Exit FH6 before running the network module comparison.")
    spec = importlib.util.spec_from_file_location("fh6_native_steam", support / "native_steam.py")
    native = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(native)
    runner = next(home / "bin" / n for n in ("highball-headless", "highball")
                  if os.access(home / "bin" / n, os.X_OK))
    steam = bottle / "drive_c/Program Files (x86)/Steam"
    client = Path.home() / "Library/Application Support/Steam"
    env = native.runtime_environment(runner, home, bottle, support / "native-bridge", engine, client, steam)
    if args.overlay:
        env["WINEDLLPATH_PREPEND"] = str(args.overlay.resolve()) + ":" + env["WINEDLLPATH_PREPEND"]
    env["WINEDLLOVERRIDES"] += ";winemenubuilder,mscoree,mshtml,winedbg=d"
    env["WINEDEBUG"] = "-all,+loaddll,+winhttp" if args.trace else "-all"
    for key in ("SteamAppId", "SteamGameId", "STEAM_COMPAT_APP_ID"):
        env.pop(key, None)
    with Server(("127.0.0.1", 0), Peer) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with args.output.open("wb") as log:
                process = subprocess.Popen([str(engine / "engine/bin/wine"), str(args.probe.resolve()),
                                            str(server.server_address[1])], env=env, cwd=support,
                                           stdout=log, stderr=subprocess.STDOUT)
                def capture_wait():
                    if process.poll() is None:
                        subprocess.run(["/usr/bin/sample", str(process.pid), "1", "1", "-file",
                                        str(args.output.with_suffix(".sample"))],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
                timer = threading.Timer(8, capture_wait)
                if args.capture_wait:
                    timer.start()
                try:
                    code = process.wait(timeout=45)
                finally:
                    timer.cancel()
        finally:
            server.shutdown()
    text = args.output.read_text(errors="replace")
    print(json.dumps({"exit_code": code, "log": str(args.output),
                      "checks": [line for line in text.splitlines()
                                 if line.startswith(("PASS ", "FAIL ", "RESULT "))]}, indent=2))
    return code


if __name__ == "__main__":
    sys.exit(main())
