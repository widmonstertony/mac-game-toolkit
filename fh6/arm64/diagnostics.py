"""Record one FH6 run without collecting network addresses or payloads."""
import collections
from datetime import datetime
import os
from pathlib import Path
import re
import subprocess
import sys
import time


GAME = "drive_c/Program Files (x86)/Steam/steamapps/common/ForzaHorizon6"
PRESENT = re.compile(rb"LocalPresent .*Frametime: (\d+(?:\.\d+)?) ms")
BASE_PRESENT = re.compile(rb"FGHooks::FGPresent flags: .*Frametime: (\d+(?:\.\d+)?)")
EXTENTS = re.compile(rb"DLSSContext (color_in|color_out|depth|mvec) extents (\([\d,]+\))")
GAME_PROCESS = re.compile(r"(?im)^\s*(\d+)\s+.*[\\/]forzahorizon6\.exe\s*$")
LOCK = re.compile(rb"RtlpWaitForCriticalSection section ([0-9a-f]+).* in thread ([0-9a-f]+), blocked by ([0-9a-f]+)", re.I)
WINE_EVENTS = {
    b"RtlpWaitForCriticalSection": "critical-section wait",
    b"RegionPolicyEvaluator": "region service activation",
    b"834366da-2d43-4fe3-8dcd-42ff2274bd0d": "gaming COM activation",
    b"SIO_UDP_NETRESET": "UDP reset option",
    b"0x8060046b": "join error",
}


def run_text(*command):
    return subprocess.run(command, capture_output=True, text=True, timeout=2).stdout


def sockets(pid):
    counts = collections.Counter()
    for line in run_text("/usr/sbin/lsof", "-nP", "-a", "-p", pid, "-i", "-F", "PT").splitlines():
        if line in ("PTCP", "PUDP", "TST=ESTABLISHED"):
            counts[line] += 1
    return dict(counts)


def new_lines(path, offsets):
    try:
        with path.open("rb") as stream:
            stat = os.fstat(stream.fileno())
            inode, offset = offsets.get(path, (stat.st_ino, 0))
            stream.seek(offset if inode == stat.st_ino and offset <= stat.st_size else 0)
            data = stream.read(4 * 1024 * 1024)
            last_line = data.rfind(b"\n") + 1
            offsets[path] = (stat.st_ino, stream.tell() - len(data) + last_line)
            return data[:last_line].splitlines()
    except FileNotFoundError:
        return []


def start_sample(pid, output, kind, report):
    sample_path = output.with_suffix(f".{kind}.sample")
    return subprocess.Popen(["/usr/bin/sample", pid, "1", "5", "-file", str(sample_path)],
                            stdout=report, stderr=report)


def monitor(bottle, output):
    game = bottle / GAME
    logs = bottle.parent.parent / "logs"
    opti = game / "OptiScaler.log"
    shim = game / "OptiScaler-FH6-SDK/fh6-metal-fsr4.log"
    wine_logs = lambda: sorted(logs.glob("*-Forza Horizon 6-steam.exe.log"),
                               key=lambda path: path.stat().st_mtime)[-3:]
    offsets = {}
    for path in (opti, shim, *wine_logs()):
        if path.exists():
            stat = path.stat()
            offsets[path] = stat.st_ino, stat.st_size
    with output.open("x", encoding="utf-8", buffering=1) as report:
        def record(message):
            print(datetime.now().isoformat(timespec="seconds"), message, file=report)

        record("armed")
        started = False
        missing = 0
        samples = {}
        recorded_locks = set()
        last_sockets = None
        deadline = time.monotonic() + 25 * 60
        while time.monotonic() < deadline:
            try:
                match = GAME_PROCESS.search(run_text("/bin/ps", "-axo", "pid,comm"))
                if match:
                    pid = match[1]
                    started, missing = True, 0
                    current = sockets(pid)
                    if current != last_sockets:
                        record(f"game pid={pid} sockets={current}")
                        last_sockets = current
                elif started:
                    missing += 1
            except (OSError, subprocess.TimeoutExpired) as error:
                record(f"process observation failed: {type(error).__name__}")
                match = None

            for line in new_lines(shim, offsets):
                if line.startswith(b"FH6 Metal FSR4 "):
                    record(line.decode("ascii", errors="replace"))

            frame_times, base_times = [], []
            counts = collections.Counter()
            for line in new_lines(opti, offsets):
                if present := PRESENT.search(line):
                    frame_times.append(float(present[1]))
                elif present := BASE_PRESENT.search(line):
                    base_times.append(float(present[1]))
                elif extents := EXTENTS.search(line):
                    record(extents[0].decode("ascii"))
                elif b"DispatchCallback D3D12_Dispatch result: 0," in line:
                    counts["fg_dispatch_ok"] += 1
                elif b"numFramesActuallyPresented: 2" in line:
                    counts["fg_present_two"] += 1
                elif b"Frame count jumped too much!" in line:
                    counts["fg_frame_jump"] += 1
                elif b"Velocity is missing" in line or b"Depth is missing" in line:
                    counts["fg_input_missing"] += 1
            if frame_times:
                frame_times.sort()
                record(f"present n={len(frame_times)} p50={frame_times[len(frame_times)//2]:.1f}ms "
                       f"max={frame_times[-1]:.1f}ms base_max={max(base_times, default=0):.1f}ms "
                       f"fg={dict(counts)}")
                if match and counts["fg_present_two"] and frame_times[-1] >= 100 and "present" not in samples:
                    samples["present"] = start_sample(match[1], output, "present", report)
                    record("sample requested after a slow present")

            events = collections.Counter()
            for path in wine_logs():
                for line in new_lines(path, offsets):
                    if lock := LOCK.search(line):
                        section, waiter, owner = (value.decode("ascii") for value in lock.groups())
                        if (section, waiter, owner) not in recorded_locks:
                            recorded_locks.add((section, waiter, owner))
                            record(f"Wine lock section={section} waiter={waiter} owner={owner}")
                    for marker, name in WINE_EVENTS.items():
                        if marker.lower() in line.lower():
                            events[name] += 1
            if events:
                record(f"Wine events {dict(events)}")
                if match and events["critical-section wait"] and "lock" not in samples:
                    samples["lock"] = start_sample(match[1], output, "lock", report)
            if missing >= 3:
                record("game exited")
                return
            time.sleep(2)
        record("capture window ended")


if __name__ == "__main__":
    monitor(Path(sys.argv[1]), Path(sys.argv[2]))
