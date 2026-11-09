"""Record one FH6 run without collecting network addresses or payloads."""
import collections
from datetime import datetime
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time


# Steam may launch the Python collector under Rosetta even though Wine uses FEX.
ARM64 = ("/usr/bin/arch", "-arm64")
GAME = "drive_c/Program Files (x86)/Steam/steamapps/common/ForzaHorizon6"
PRESENT = re.compile(rb"LocalPresent .*Frametime: (\d+(?:\.\d+)?) ms")
BASE_PRESENT = re.compile(rb"FGHooks::FGPresent flags: .*Frametime: (\d+(?:\.\d+)?)")
PHASE = re.compile(
    rb"^\[(\d{2}):(\d{2}):(\d{2}\.\d+)\].*?"
    rb"(FGHooks::FGPresent flags:|ResTrack_Dx12::ClearPossibleHudless|"
    rb"FSRFG_Dx12::DispatchCallback frameID:|FSRFG_Dx12::DispatchCallback D3D12_Dispatch result:|"
    rb"FGHooks::FGPresent Present finished|LocalPresent .*Frametime:)"
)
PHASES = (("fg_prepare", "entry", "ready"), ("swapchain_to_callback", "ready", "callback"),
          ("fg_cpu_submit", "callback", "submitted"), ("swapchain_to_first_present", "ready", "first"),
          ("swapchain_to_last_present", "ready", "last"), ("fg_present_total", "entry", "finished"))
PHASE_MARKS = {
    b"FGHooks::FGPresent flags:": "entry",
    b"ResTrack_Dx12::ClearPossibleHudless": "ready",
    b"FSRFG_Dx12::DispatchCallback frameID:": "callback",
    b"FSRFG_Dx12::DispatchCallback D3D12_Dispatch result:": "submitted",
    b"FGHooks::FGPresent Present finished": "finished",
}
GPU_METRICS = re.compile(r'"(Device|Renderer|Tiler) Utilization %"\s*=\s*(\d+)')
EXTENTS = re.compile(rb"DLSSContext (color_in|color_out|depth|mvec) extents (\([\d,]+\))")
GAME_PROCESS = re.compile(r"(?im)^\s*(\d+)\s+.*[\\/]forzahorizon6\.exe\s*$")
LOCK = re.compile(rb"RtlpWaitForCriticalSection section ([0-9a-f]+).* in thread ([0-9a-f]+), blocked by ([0-9a-f]+)", re.I)
WINE_EVENTS = {
    b"RtlpWaitForCriticalSection": "critical-section wait",
    b"RegionPolicyEvaluator": "region service activation",
    b"834366da-2d43-4fe3-8dcd-42ff2274bd0d": "gaming COM activation",
    b"SIO_UDP_NETRESET": "UDP reset option",
    b"0x8060046b": "join error",
    b"it failed with PageFault": "Metal GPU page fault",
    b"VK_ERROR_DEVICE_LOST": "Vulkan device lost",
    b"exceeded compiled variants footprint": "Metal compiled-variant footprint limit",
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
    return subprocess.Popen([*ARM64, "/usr/bin/sample", pid, "1", "5", "-file", str(sample_path)],
                            stdout=report, stderr=report)


def frame_phase(line, frame, timings):
    match = PHASE.search(line)
    if not match:
        return
    h, m, sec, label = match.groups()
    now = (int(h) * 3600 + int(m) * 60 + float(sec)) * 1000
    mark = PHASE_MARKS.get(label)
    if mark == "entry":
        frame.clear()
    elif "entry" not in frame:
        return
    if mark is None:
        frame.setdefault("first", now)
        frame["last"] = now
    else:
        frame[mark] = now
    if mark == "finished":
        for name, start, end in PHASES:
            if start in frame and end in frame:
                elapsed = (frame[end] - frame[start]) % 86400000
                if elapsed < 30000:
                    timings[name].append(elapsed)
        frame.clear()


def stats(values):
    ordered = sorted(values)
    return (f"n={len(ordered)} p50={ordered[len(ordered)//2]:.2f} "
            f"p95={ordered[(len(ordered)*95-1)//100]:.2f} max={ordered[-1]:.2f} ms")


def start_metal_trace(pid, output, report):
    if shutil.disk_usage(output.parent).free < 3 * 1024 ** 3:
        report("Metal trace skipped: less than 3 GiB free")
        return None
    trace = output.with_suffix(".metal.trace")
    env = dict(os.environ, DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer")
    with output.with_suffix(".metal.status").open("x", encoding="utf-8") as status:
        process = subprocess.Popen(
            [*ARM64, "/usr/bin/xcrun", "xctrace", "record", "--template", "Metal System Trace",
             "--attach", pid, "--time-limit", "8s", "--no-prompt", "--output", str(trace)],
            stdin=subprocess.DEVNULL, stdout=status, stderr=status, env=env,
        )
    report(f"Metal trace requested for pid={pid}")
    return process


def finish_metal_trace(process):
    if process.poll() is None:
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    return process.returncode



def monitor(bottle, output):
    game = bottle / GAME
    logs = bottle.parent.parent / "logs"
    opti = game / "OptiScaler.log"
    shim = game / "OptiScaler-FH6-SDK/fh6-metal-fsr4.log"
    lobby = game / "OptiScaler-FH6-SDK/fh6-session-results.log"
    framegen = game / "OptiScaler-FH6-SDK/fh6-framegen-motion.log"
    wine_logs = lambda: sorted(logs.glob("*-Forza Horizon 6-steam.exe.log"),
                               key=lambda path: path.stat().st_mtime)[-3:]
    native_log = output.parent / "arm64-dispatch.log"
    offsets = {}
    for path in (opti, shim, lobby, framegen, native_log, *wine_logs()):
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
        frame = {}
        slow_windows = 0
        metal_attempted = False
        metal_trace = None
        metal_deadline = 0
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
            for line in new_lines(lobby, offsets):
                record(line.decode("ascii", errors="replace"))
            for line in new_lines(framegen, offsets):
                record(line.decode("ascii", errors="replace"))

            frame_times, base_times = [], []
            counts = collections.Counter()
            timings = collections.defaultdict(list)
            for line in new_lines(opti, offsets):
                frame_phase(line, frame, timings)
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
                record(f"present {stats(frame_times)} fg={dict(counts)}")
            if base_times:
                record(f"base frame {stats(base_times)}")
            for name, values in timings.items():
                record(f"phase {name} {stats(values)}")
            if match and len(base_times) >= 10 and counts["fg_present_two"]:
                slow = sorted(base_times)[len(base_times)//2] >= 35
                slow_windows = slow_windows + 1 if slow else 0
                try:
                    cpu = run_text("/bin/ps", "-p", match[1], "-o", "%cpu=,rss=").split()
                    gpu = dict(GPU_METRICS.findall(run_text("/usr/sbin/ioreg", "-r", "-c",
                                                          "IOAccelerator", "-l", "-w", "0")))
                    if len(cpu) == 2:
                        record(f"load cpu_pct={cpu[0]} rss_kib={cpu[1]} system_gpu_pct={gpu}")
                except (OSError, subprocess.TimeoutExpired):
                    record("CPU/GPU load observation unavailable")
                if slow_windows and "steady" not in samples:
                    samples["steady"] = start_sample(match[1], output, "steady", report)
                    record("sample requested during slow gameplay frames")
                if (slow_windows >= 3 and not metal_attempted and
                        ("steady" not in samples or samples["steady"].poll() is not None)):
                    metal_attempted = True
                    try:
                        metal_trace = start_metal_trace(match[1], output, record)
                        metal_deadline = time.monotonic() + 35
                    except OSError as error:
                        record(f"Metal trace could not start: {error}")
            else:
                slow_windows = 0
            if metal_trace and (metal_trace.poll() is not None or time.monotonic() >= metal_deadline):
                record(f"Metal trace finished status={finish_metal_trace(metal_trace)}")
                metal_trace = None
            if (match and frame_times and counts["fg_present_two"] and max(frame_times) >= 100
                    and "present" not in samples and not metal_trace):
                samples["present"] = start_sample(match[1], output, "present", report)
                record("sample requested after a slow present")

            events = collections.Counter()
            for path in (native_log, *wine_logs()):
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
                if metal_trace:
                    record(f"Metal trace finished on game exit status={finish_metal_trace(metal_trace)}")
                record("game exited")
                return
            time.sleep(2)
        if metal_trace:
            record(f"Metal trace finished on capture deadline status={finish_metal_trace(metal_trace)}")
        record("capture window ended")


if __name__ == "__main__":
    monitor(Path(sys.argv[1]), Path(sys.argv[2]))
