#!/usr/bin/env python3
"""Enable fullscreen pointer confinement for Stellar Blade alone in Protonish."""
from datetime import datetime
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from sky_yyb_fix import atomic_write

SWITCH = "NOTPROTON_STELLAR_POINTER"
TOOL = "protonish-highball-x64-crossover26.3-r28"
APP = re.compile(r'(?ms)^(\t{5}"3489700"\s*\n\t{5}\{\n)(.*?)(^\t{5}\})')
OPTION = re.compile(r'(?m)^(\t{6}"LaunchOptions"\s+")((?:\\.|[^"\\\n])*)("[^\n]*\n?)')


def enable(text):
    matches = list(APP.finditer(text))
    if len(matches) != 1:
        raise ValueError("Expected exactly one Stellar Blade entry in the Steam account.")
    match = matches[0]
    body = match.group(2)
    options = OPTION.search(body)
    old = options.group(2) if options else "%command%"
    if "%command%" not in old:
        raise ValueError("Unexpected Stellar Blade launch options; left unchanged.")
    value = re.sub(r'(?<!\S)' + SWITCH + r'=\S+\s*', '', old)
    value = SWITCH + "=1 " + value
    if options:
        body = OPTION.sub(lambda m: m.group(1) + value + m.group(3), body, count=1)
    else:
        body = f'\t\t\t\t\t\t"LaunchOptions"\t\t"{value}"\n' + body
    updated = text[:match.start(2)] + body + text[match.end(2):]
    return updated, old, value


def plugin_text(component):
    return (
        f"[{SWITCH}]\n"
        "label = Stellar Blade fullscreen pointer confinement\n"
        "description = Keep the pointer in the focused fullscreen game; release on app switch.\n"
        "section = dev\n"
        f"tools = {TOOL}\n"
        "on = 1\ninject = 1\n"
        f"requires = {component}\n"
        "env = HB_STELLAR_CONFINEMENT=1\n"
    )


def main():
    steam = Path.home() / "Library/Application Support/Steam"
    support = Path.home() / "Library/Application Support/protonish"
    if not (steam / "compatibilitytools.d" / TOOL / "run").is_file():
        sys.exit("The supported Protonish Highball tool is not installed.")
    accounts = []
    for path in steam.glob("userdata/*/config/localconfig.vdf"):
        text = path.read_text()
        if APP.search(text):
            accounts.append((path, text))
    if len(accounts) != 1:
        sys.exit("Expected one local Steam account with Stellar Blade configured.")
    path, text = accounts[0]
    updated, old, options = enable(text)
    processes = subprocess.check_output(["/bin/ps", "-axo", "comm="], text=True)
    if any(line.strip().lower().endswith(("/sb-win64-shipping.exe", "/stellarblade.exe"))
           for line in processes.splitlines()):
        sys.exit("Exit Stellar Blade before changing its pointer component.")
    if updated != text and any(line.strip().endswith("/steam_osx") for line in processes.splitlines()):
        sys.exit("Exit native Steam before changing its per-game launch options.")
    component = support / "game-components/3489700/libstellar-confinement.dylib"
    plugin = support / "plugins/87-stellar-pointer.conf"
    with tempfile.TemporaryDirectory(prefix="stellar-pointer-") as work:
        binary = Path(work) / component.name
        subprocess.run([
            "clang", "-O2", "-Wall", "-Wextra", "-Werror", "-fobjc-arc",
            "-dynamiclib", "-arch", "x86_64", "-arch", "arm64",
            "-mmacosx-version-min=14.0", "-framework", "AppKit",
            str(Path(__file__).with_name("stellar-confinement.m")), "-o", str(binary),
        ], check=True)
        subprocess.run(["codesign", "--force", "--sign", "-", str(binary)], check=True)
        subprocess.run(["codesign", "--verify", str(binary)], check=True)
        if updated != text:
            record = support / "backups" / datetime.now().strftime("stellar-pointer-%Y%m%d-%H%M%S-%f")
            record.mkdir(parents=True)
            # Record only this game's options, never account credentials or game data.
            atomic_write(record / "launch-options.json", json.dumps({
                "app_id": "3489700", "before": old, "after": options,
            }, ensure_ascii=False, indent=2).encode(), 0o600)
        component.parent.mkdir(parents=True, exist_ok=True)
        plugin.parent.mkdir(parents=True, exist_ok=True)
        atomic_write(component, binary.read_bytes(), 0o755)
        atomic_write(plugin, plugin_text(component).encode(), 0o600)
        if updated != text:
            atomic_write(path, updated.encode(), path.stat().st_mode & 0o777)
    print("Stellar Blade fullscreen pointer confinement installed; other games unchanged.")


if __name__ == "__main__":
    main()
