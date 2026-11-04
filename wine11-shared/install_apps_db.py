#!/usr/bin/env python3
"""Add the shared Wine 11 launchers to Application Treasure's JSON database."""

from __future__ import annotations

import json
import os
import shutil
import sys
import time
from datetime import datetime
from pathlib import Path


def install(db_path: Path) -> Path:
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_path = db_path.with_name(f"{db_path.name}.codex-before-launchers-{timestamp}")
    shutil.copy2(db_path, backup_path)

    with db_path.open("r", encoding="utf-8") as handle:
        apps = json.load(handle)
    if not isinstance(apps, dict):
        raise ValueError("apps.db is not a JSON object")

    now = int(time.time())
    base = {
        "app_select_path": "",
        "display_version": "Wine 11",
        "entry_exe_version_code": "",
        "entry_exe_version_name": "",
        "from_app_name": "本地",
        "game_entry_path": "",
        "game_id": "",
        "install_from": "from_local_install",
        "install_time": now,
        "launcher_child_exe": "",
        "launcher_id": "",
        "launcher_metadata_version": 0,
        "launcher_package": "",
        "need_shortcut": 1,
        "shortcut_args": "",
        "shortcut_scene": "",
        "shortcut_type": "",
        "uninstall_key_name": "",
        "uninstall_path": "",
        "version_code": 1,
        "version_name": "Wine 11",
    }
    definitions = {
        "com.tencent.macexe.com.yybea11": (
            "EA App", "YYBEA11Launcher.exe", "Electronic Arts",
            r"C:\Program Files\Electronic Arts\EA Desktop",
        ),
        "com.tencent.macexe.com.yybepic11": (
            "Epic Games", "YYBEpic11Launcher.exe", "Epic Games, Inc.",
            r"C:\Program Files (x86)\Epic Games\Launcher",
        ),
        "com.tencent.macexe.com.yybrockstar11": (
            "Rockstar Games Launcher", "YYBRockstar11Launcher.exe", "Rockstar Games",
            r"C:\Program Files\Rockstar Games\Launcher",
        ),
    }
    fallback_icon = next(
        (
            record.get("icon_path", "")
            for record in apps.values()
            if isinstance(record, dict)
            and record.get("icon_path")
            and Path(record["icon_path"]).exists()
        ),
        "",
    )

    for key, (name, launcher, publisher, install_path) in definitions.items():
        record = dict(base)
        shortcut = rf"C:\YYBFH6\{launcher}"
        record.update(
            entry_path=shortcut,
            exe_name=name,
            icon_path=fallback_icon,
            install_path=install_path,
            publisher=publisher,
            shortcut_path=shortcut,
        )
        apps[key] = record

    temporary_path = db_path.with_name(f".{db_path.name}.{os.getpid()}.tmp")
    try:
        with temporary_path.open("w", encoding="utf-8") as handle:
            json.dump(apps, handle, ensure_ascii=False, indent=4, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_path, db_path)
    finally:
        temporary_path.unlink(missing_ok=True)
    return backup_path


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} /path/to/apps.db")
    backup = install(Path(sys.argv[1]).expanduser())
    print(f"apps.db backup: {backup}")
