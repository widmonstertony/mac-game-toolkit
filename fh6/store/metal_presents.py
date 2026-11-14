"""Correlate a game's Metal requests, command completions and drawable callbacks."""

import argparse
from bisect import bisect_left
from collections import Counter, defaultdict
import json
import os
from pathlib import Path
from statistics import median
import subprocess
import sys
import xml.etree.ElementTree as ET


REQUIRED_TABLES = (
    "ca-client-present-request",
    "metal-application-command-buffer-submissions",
    "metal-command-buffer-completed",
)
OPTIONAL_TABLES = (
    "ca-client-presented-handler",
    "metal-gpu-intervals",
    "ca-client-buffer-wait-interval",
    "time-profile",
    "thread-state",
)


def rows(path, *fields):
    for nodes, _ in row_nodes(path, fields):
        yield tuple(node.text or node.get("fmt") for node in nodes)


def row_nodes(path, fields):
    root = ET.parse(path).getroot()
    columns = [column.findtext("mnemonic") for column in root.findall(".//schema/col")]
    indices = [columns.index(field) for field in fields]
    references = {node.attrib["id"]: node for node in root.iter() if "id" in node.attrib}
    for row in root.findall(".//row"):
        nodes = [row[index] for index in indices]
        resolved = [references[node.attrib["ref"]] if "ref" in node.attrib else node for node in nodes]
        yield resolved, references


def thread_states(path, threads, earliest, latest):
    fields = ("start", "thread", "state", "duration", "made-runnable-by-thread")
    states = defaultdict(list)
    references = {}
    for event, node in ET.iterparse(path, events=("start", "end")):
        if event == "start" and node.tag == "node":
            parent = node
        if event != "end":
            continue
        if node.tag == "schema":
            columns = [column.findtext("mnemonic") for column in node.findall("col")]
            indices = [columns.index(field) for field in fields]
        if node.tag != "row":
            continue
        # A definition can occur on a row belonging to a different thread.
        for field in node.iter():
            if (field.tag in {"thread", "start-time", "thread-state", "duration"}
                    and (key := field.get("id")) is not None):
                references[key] = field.text or field.get("fmt")
        stamp, thread, state, duration, waker = (
            references[field.get("ref")] if "ref" in field.attrib else field.text or field.get("fmt")
            for field in (node[index] for index in indices)
        )
        if thread in threads and stamp and duration and state:
            first, last = int(stamp), int(stamp) + int(duration)
            if first < latest and last >= earliest:
                states[thread].append((first, last, state, waker))
        parent.remove(node)
    return states


def gap_scheduler(states, start, end, profile):
    residency = Counter()
    wakes = Counter()
    last_wake = None
    for first, last, state, waker in states:
        overlap = min(last, end) - max(first, start)
        if overlap > 0:
            residency[state] += overlap
        if waker and start <= first < end:
            wakes[waker] += 1
            if last_wake is None or first > last_wake[0]:
                last_wake = first, waker
    resume = None
    if last_wake:
        stamp, waker = last_wake
        resume = {"trace_seconds": round(stamp / 1e9, 6), "waker": waker}
        if profile is not None:
            nearby = [(time, stack) for time, _, stack in profile.get(waker, ())
                      if abs(time - stamp) <= 2_000_000 and stack]
            if nearby:
                time, stack = min(nearby, key=lambda sample: abs(sample[0] - stamp))
                resume["nearest_stack_sample"] = {"offset_ms": ms(time - stamp), "frames": stack}
    return {
        "state_ms": {state: ms(elapsed) for state, elapsed in residency.items()},
        "sampled_ms": ms(sum(residency.values())),
        "recorded_wakers": dict(wakes.most_common(6)),
        "last_recorded_wake": resume,
    }


def covered_ns(intervals, start, end):
    covered = 0
    cursor = start
    for first, last in intervals:
        if first >= end:
            break
        first, last = max(start, first), min(end, last)
        if last > max(cursor, first):
            covered += last - max(cursor, first)
            cursor = last
    return covered


def ms(value):
    return round(value / 1_000_000, 3)


def summarize(directory, pid, threshold_ms):
    def owned(process):
        return process is not None and process.endswith(f" ({pid})")

    requests = sorted(
        (int(timestamp), int(command))
        for timestamp, command, process in rows(
            directory / "ca-client-present-request.xml", "timestamp", "cmdbuffer-id", "process"
        )
        if owned(process) and timestamp and command
    )
    command_ids = {command for _, command in requests}
    submissions = defaultdict(list)
    title_submissions = []
    for timestamp, command, process, thread in rows(
        directory / "metal-application-command-buffer-submissions.xml", "start", "cmdbuffer-id", "process", "thread"
    ):
        if owned(process) and timestamp and command:
            command, timestamp = int(command), int(timestamp)
            title_submissions.append(timestamp)
            if command in command_ids:
                submissions[command].append((timestamp, thread))
    title_submissions.sort()
    completions = defaultdict(list)
    for timestamp, command in rows(directory / "metal-command-buffer-completed.xml", "timestamp", "cmdbuffer-id"):
        if timestamp and command and int(command) in command_ids:
            completions[int(command)].append(int(timestamp))

    # Drop incomplete trace edges and ambiguous buffer IDs.
    matched = {}
    for command in command_ids:
        starts, ends = submissions[command], completions[command]
        if len(starts) == len(ends) == 1 and starts[0][0] <= ends[0]:
            matched[command] = starts[0][0], ends[0], starts[0][1]

    gpu_file = directory / "metal-gpu-intervals.xml"
    gpu_activity = sorted(
        (int(timestamp), int(timestamp) + int(duration))
        for timestamp, duration, process, state in rows(gpu_file, "start", "duration", "process", "state")
        if owned(process) and timestamp and duration and state == "Active" and int(duration) > 0
    ) if gpu_file.is_file() else None

    waits_file = directory / "ca-client-buffer-wait-interval.xml"
    drawable_waits = defaultdict(list) if waits_file.is_file() else None
    if drawable_waits is not None:
        for timestamp, duration, thread in rows(waits_file, "start", "duration", "thread"):
            if thread and thread.endswith(f"pid: {pid})") and timestamp and duration and int(duration) > 0:
                drawable_waits[thread].append((int(timestamp), int(timestamp) + int(duration)))
        for spans in drawable_waits.values():
            spans.sort()

    profile_file = directory / "time-profile.xml"
    profile = defaultdict(list) if profile_file.is_file() else None
    if profile is not None:
        for nodes, refs in row_nodes(profile_file, ("time", "thread", "process", "thread-state", "stack")):
            stamp, thread, process, thread_state, stack = nodes
            if not owned(process.get("fmt")) or not stamp.text or not thread.get("fmt"):
                continue
            frames = tuple(
                resolved.get("name") for frame in stack.findall("frame")[:10]
                if (resolved := refs[frame.attrib["ref"]] if "ref" in frame.attrib else frame).get("name")
            )
            profile[thread.get("fmt")].append((int(stamp.text), thread_state.text, frames))

    scheduler_file = directory / "thread-state.xml"
    gap_windows = [
        (previous, current, matched[next_id][2])
        for (previous, _), (current, next_id) in zip(requests, requests[1:])
        if current - previous >= threshold_ms * 1_000_000 and next_id in matched
    ]
    scheduler = None
    if scheduler_file.is_file() and gap_windows:
        scheduler = thread_states(scheduler_file, {thread for _, _, thread in gap_windows},
                                  gap_windows[0][0], gap_windows[-1][1])

    intervals = []
    gaps = []
    for (previous, prior_id), (current, next_id) in zip(requests, requests[1:]):
        delta = current - previous
        intervals.append(delta)
        if delta < threshold_ms * 1_000_000:
            continue
        earlier = matched.get(prior_id)
        later = matched.get(next_id)
        thread = later[2] if later else None
        submitter_samples = None
        if profile is not None and thread:
            samples = [(state, frames[0] if frames else None)
                       for stamp, state, frames in profile[thread] if previous <= stamp < current]
            submitter_samples = {
                "count": len(samples),
                "states": dict(Counter(state for state, _ in samples if state)),
                "top_symbols": dict(Counter(symbol for _, symbol in samples if symbol).most_common(5)),
            }
        gaps.append({
            "trace_seconds": round(current / 1_000_000_000, 6),
            "request_gap_ms": ms(delta),
            "previous_completion_after_request_ms": ms(earlier[1] - previous) if earlier else None,
            "next_submission_after_previous_completion_ms": ms(later[0] - earlier[1]) if earlier and later else None,
            "next_completion_after_submission_ms": ms(later[1] - later[0]) if later else None,
            "title_submissions_in_gap": bisect_left(title_submissions, current) - bisect_left(title_submissions, previous),
            "title_gpu_active_ms": ms(covered_ns(gpu_activity, previous, current)) if gpu_activity is not None else None,
            "submitter_drawable_wait_ms": ms(covered_ns(drawable_waits[thread], previous, current))
            if drawable_waits is not None and thread else None,
            "submitter_time_profiler": submitter_samples,
            "submitter_thread": thread,
            "submitter_scheduler": gap_scheduler(scheduler[thread], previous, current, profile)
            if scheduler is not None and thread else None,
        })

    callbacks_file = directory / "ca-client-presented-handler.xml"
    # Callback time is separate from the drawable's recorded presentation time.
    # Exclude missing or zero at-time records and never count a duplicate time.
    displayed = sorted({
        int(at) for at, process in rows(callbacks_file, "at-time", "process")
        if owned(process) and at and int(at) > 0
    }) if callbacks_file.is_file() else []
    display_intervals = [current - previous for previous, current in zip(displayed, displayed[1:])]
    display_gaps = [value for value in display_intervals if value >= threshold_ms * 1_000_000]
    return {
        "present_requests": len(requests),
        "requests_with_matched_submission_and_completion": sum(command in matched for _, command in requests),
        "title_gpu_active_intervals": len(gpu_activity) if gpu_activity is not None else None,
        "title_drawable_wait_intervals": sum(map(len, drawable_waits.values())) if drawable_waits is not None else None,
        "median_request_interval_ms": ms(median(intervals)) if intervals else None,
        "long_request_gaps": len(gaps),
        "longest_request_gaps": sorted(gaps, key=lambda gap: gap["request_gap_ms"], reverse=True)[:20],
        "drawable_presentation_times": len(displayed),
        "drawable_capture_seconds": round((displayed[-1] - displayed[0]) / 1e9, 3)
        if display_intervals else None,
        "drawable_presentation_rate_hz": round(len(display_intervals) * 1e9 / (displayed[-1] - displayed[0]), 2)
        if display_intervals else None,
        "drawable_presentation_long_gaps": len(display_gaps) if display_intervals else None,
        "drawable_presentation_max_interval_ms": ms(max(display_intervals)) if display_intervals else None,
    }


def record(directory, pid, seconds):
    env = os.environ.copy()
    xcode = Path("/Applications/Xcode.app/Contents/Developer")
    if xcode.is_dir():
        env.setdefault("DEVELOPER_DIR", str(xcode))
    directory.mkdir(mode=0o700)
    trace = directory / "roam.trace"
    command = ["/usr/bin/xcrun", "xctrace"]
    subprocess.run([*command, "record", "--template", "Metal System Trace", "--instrument", "Thread Activity",
                    "--attach", str(pid),
                    "--time-limit", f"{seconds}s", "--no-prompt", "--output", trace],
                   check=True, env=env, timeout=seconds + 30, stdout=sys.stderr)
    toc = directory / "toc.xml"
    subprocess.run([*command, "export", "--input", trace, "--toc", "--output", toc],
                   check=True, env=env, timeout=30, stdout=sys.stderr)
    present = {table.get("schema") for table in ET.parse(toc).findall(".//run[@number='1']/data/table")}
    missing = set(REQUIRED_TABLES + ("thread-state",)) - present
    if missing:
        raise ValueError("Trace lacks required tables: " + ", ".join(sorted(missing)))
    for schema in (*REQUIRED_TABLES, *OPTIONAL_TABLES):
        if schema in present:
            subprocess.run([*command, "export", "--input", trace,
                            "--xpath", f"/trace-toc/run[@number='1']/data/table[@schema='{schema}']",
                            "--output", directory / (schema + ".xml")], check=True, env=env, timeout=90,
                           stdout=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="existing export directory, or new output directory with --record")
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--threshold-ms", type=float, default=120)
    parser.add_argument("--record", action="store_true", help="attach to the existing process and export the trace")
    parser.add_argument("--seconds", type=int, default=15, help="recording duration with --record")
    args = parser.parse_args()
    if args.pid <= 0 or not 0 < args.threshold_ms <= 60000:
        parser.error("PID must be positive and threshold-ms must be in (0, 60000]")
    if not 5 <= args.seconds <= 30:
        parser.error("seconds must be in [5, 30]")
    try:
        if args.record:
            record(args.directory, args.pid, args.seconds)
        summary = summarize(args.directory, args.pid, args.threshold_ms)
    except (ET.ParseError, OSError, ValueError, KeyError, IndexError, subprocess.SubprocessError) as exc:
        parser.exit(2, f"Unable to read xctrace tables: {exc}\n")
    print(json.dumps(summary, indent=2))
    return 0 if summary["present_requests"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
