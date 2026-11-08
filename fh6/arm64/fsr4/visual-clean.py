#!/usr/bin/env python3
"""Keep native Metal FSR 4 and prioritize clear base frames in FH6."""
import argparse
from datetime import datetime
import os
from pathlib import Path
import re
import tempfile


GAME = "drive_c/Program Files (x86)/Steam/steamapps/common/ForzaHorizon6"
UPDATES = {
    "FrameGen": {"Enabled": "false"},
    "QualityOverrides": {"QualityRatioOverrideEnabled": "true", "QualityRatioPerformance": "1.5"},
    "Log": {"LogLevel": "2"},
}


def update_config(text, frame_generation=False, quality_ratio=1.5):
    if not re.search(r"(?m)^Dx12Upscaler\s*=\s*fsr31\r?$", text):
        raise ValueError("Expected the existing OptiScaler FidelityFX path")
    for section, updates in UPDATES.items():
        if frame_generation and section == "FrameGen":
            updates = {"Enabled": "true"}
        elif frame_generation and section == "Log":
            updates = {"LogLevel": "1"}
        elif section == "QualityOverrides":
            updates = {**updates, "QualityRatioPerformance": str(quality_ratio)}
        pattern = rf"(?m)^\[{section}\]\r?\n[^\[]*"
        match = re.search(pattern, text)
        if not match:
            raise ValueError(f"Missing OptiScaler section {section}")
        updated = match[0]
        for key, value in updates.items():
            updated, count = re.subn(rf"(?m)^({key}[ \t]*=[ \t]*)[^\r\n]*",
                                     lambda setting: setting[1] + value, updated)
            if count != 1:
                raise ValueError(f"Expected exactly one {section}.{key} setting")
        text = text[:match.start()] + updated + text[match.end():]
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--with-frame-generation", action="store_true",
                        help="keep 2× enabled and log its dispatch and present results")
    parser.add_argument("--quality-ratio", type=float, default=1.5)
    options = parser.parse_args()
    if not 1.0 <= options.quality_ratio <= 2.0:
        parser.error("--quality-ratio must be between 1.0 and 2.0")
    bottle = Path.home() / "Library/Application Support/Highball/bottles/Forza Horizon 6"
    path = bottle / GAME / "OptiScaler.ini"
    old = path.read_bytes()
    new = update_config(old.decode("utf-8"), options.with_frame_generation,
                        options.quality_ratio).encode("utf-8")
    if old == new:
        return
    backup = bottle / ".fh6-metal-fsr4" / f"OptiScaler-before-clear-{datetime.now():%Y%m%d-%H%M%S-%f}.ini"
    with backup.open("xb") as original:
        original.write(old)
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".fh6-visual-", delete=False) as stream:
        pending = Path(stream.name)
        stream.write(new)
    try:
        pending.chmod(path.stat().st_mode & 0o777)
        if path.read_bytes() != old:
            raise RuntimeError("OptiScaler settings changed while staging the visual profile")
        os.replace(pending, path)
    finally:
        pending.unlink(missing_ok=True)
    print(f"FH6 visual settings staged. Previous settings: {backup}")


if __name__ == "__main__":
    main()
