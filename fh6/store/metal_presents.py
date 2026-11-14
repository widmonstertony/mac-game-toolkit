"""Correlate a game's Metal requests, command completions and drawable callbacks."""

import argparse
from collections import defaultdict
import json
from pathlib import Path
from statistics import median
import xml.etree.ElementTree as ET


def rows(path, *fields):
    root = ET.parse(path).getroot()
    columns = [column.findtext("mnemonic") for column in root.findall(".//schema/col")]
    indices = [columns.index(field) for field in fields]
    references = {node.attrib["id"]: node for node in root.iter() if "id" in node.attrib}
    for row in root.findall(".//row"):
        nodes = [row[index] for index in indices]
        resolved = [references[node.attrib["ref"]] if "ref" in node.attrib else node for node in nodes]
        yield tuple(node.text or node.get("fmt") for node in resolved)


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
    for timestamp, command, process in rows(
        directory / "metal-application-command-buffer-submissions.xml", "start", "cmdbuffer-id", "process"
    ):
        if owned(process) and timestamp and command and int(command) in command_ids:
            submissions[int(command)].append(int(timestamp))
    completions = defaultdict(list)
    for timestamp, command in rows(directory / "metal-command-buffer-completed.xml", "timestamp", "cmdbuffer-id"):
        if timestamp and command and int(command) in command_ids:
            completions[int(command)].append(int(timestamp))

    # Drop incomplete trace edges and ambiguous buffer IDs.
    matched = {}
    for command in command_ids:
        starts, ends = submissions[command], completions[command]
        if len(starts) == len(ends) == 1 and starts[0] <= ends[0]:
            matched[command] = starts[0], ends[0]

    intervals = []
    gaps = []
    for (previous, prior_id), (current, next_id) in zip(requests, requests[1:]):
        delta = current - previous
        intervals.append(delta)
        if delta < threshold_ms * 1_000_000:
            continue
        earlier = matched.get(prior_id)
        later = matched.get(next_id)
        gaps.append({
            "trace_seconds": round(current / 1_000_000_000, 6),
            "request_gap_ms": ms(delta),
            "previous_completion_after_request_ms": ms(earlier[1] - previous) if earlier else None,
            "next_submission_after_previous_completion_ms": ms(later[0] - earlier[1]) if earlier and later else None,
            "next_completion_after_submission_ms": ms(later[1] - later[0]) if later else None,
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="directory containing exported xctrace XML tables")
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--threshold-ms", type=float, default=120)
    args = parser.parse_args()
    if args.pid <= 0 or not 0 < args.threshold_ms <= 60000:
        parser.error("PID must be positive and threshold-ms must be in (0, 60000]")
    try:
        summary = summarize(args.directory, args.pid, args.threshold_ms)
    except (ET.ParseError, OSError, ValueError, KeyError, IndexError) as exc:
        parser.exit(2, f"Unable to read xctrace tables: {exc}\n")
    print(json.dumps(summary, indent=2))
    return 0 if summary["present_requests"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
