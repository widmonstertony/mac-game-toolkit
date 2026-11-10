#!/usr/bin/env python3
"""Install the Highball audio-clock scheduling fix for native Steam Wukong."""

from datetime import datetime
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from sky_yyb_fix import atomic_write


def main():
    processes = subprocess.check_output(["/bin/ps", "-axo", "comm="], text=True)
    if any(line.strip().endswith("/steam_osx") for line in processes.splitlines()):
        sys.exit("Exit native Steam before updating its per-game launch options.")

    steam = Path.home() / "Library/Application Support/Steam"
    block = re.compile(r'(?ms)^(\t{5}"2358720"\s*\n\t{5}\{\n)(.*?)(^\t{5}\})')
    accounts = []
    for path in steam.glob("userdata/*/config/localconfig.vdf"):
        text = path.read_text()
        match = block.search(text)
        if match:
            accounts.append((path, text, match))
    if len(accounts) != 1:
        sys.exit("Expected one Steam account with Wukong installed.")

    support = Path.home() / "Library/Application Support/protonish"
    component = support / "game-components/2358720/libwukong-audiort.dylib"
    plugin = support / "plugins/86-wukong-audiort.conf"
    switch = "NOTPROTON_WUKONG_AUDIO_RT=1"
    plugin_text = (
        "[NOTPROTON_WUKONG_AUDIO_RT]\n"
        "label = Wine audio clock scheduling\n"
        "description = Real-time scheduling for the Wine audio clock thread.\n"
        "section = dev\n"
        "tools = protonish-highball-x64-crossover26.3-r28\n"
        "on = 1\n"
        "inject = 1\n"
        f"requires = {component}\n"
        "env = HB_AUDIOBUF_DEBUG=1\n"
    )
    with tempfile.TemporaryDirectory(prefix="wukong-audiort-build-") as work:
        binary = Path(work) / component.name
        source = Path(__file__).with_name("audiort.c")
        subprocess.run([
            "clang", "-O2", "-Wall", "-Wextra", "-Werror",
            "-arch", "x86_64", "-arch", "arm64", "-dynamiclib",
            "-mmacosx-version-min=14.0", str(source), "-o", str(binary),
        ], check=True)
        subprocess.run(["codesign", "--force", "--sign", "-", str(binary)], check=True)
        subprocess.run(["codesign", "--verify", str(binary)], check=True)

        path, text, match = accounts[0]
        body = match.group(2)
        setting = re.compile(r'(?m)^\t{6}"LaunchOptions"\s+"([^"\n]*)"[^\n]*\n?')
        current = setting.search(body)
        options = current.group(1) if current else "%command%"
        options = re.sub(r'\bNOTPROTON_WUKONG_AUDIO_RT=\S+\s*', '', options)
        options = switch + " " + options
        line = f'\t\t\t\t\t\t"LaunchOptions"\t\t"{options}"\n'
        body = setting.sub(lambda _: line, body) if current else line + body
        updated = text[:match.start()] + match.group(1) + body + match.group(3) + text[match.end():]

        backup = support / "backups" / datetime.now().strftime("wukong-audio-%Y%m%d-%H%M%S-%f")
        backup.mkdir(parents=True)
        shutil.copy2(path, backup / "localconfig.vdf")
        if plugin.exists():
            shutil.copy2(plugin, backup / plugin.name)
        if component.exists():
            shutil.copy2(component, backup / component.name)
        component.parent.mkdir(parents=True, exist_ok=True)
        plugin.parent.mkdir(parents=True, exist_ok=True)
        atomic_write(component, binary.read_bytes(), 0o755)
        atomic_write(plugin, plugin_text.encode(), 0o600)
        atomic_write(path, updated.encode(), path.stat().st_mode & 0o777)

    print("Wukong audio clock fix installed.")
    print("Launch options", options)
    print("Backup", backup)


if __name__ == "__main__":
    main()
