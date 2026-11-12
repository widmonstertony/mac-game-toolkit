#!/usr/bin/env python3
"""Install the verified per-game Wukong scheduling/buffering audio fix."""

from datetime import datetime
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from sky_yyb_fix import atomic_write


def main():
    # audiort.c reads private stream fields: the tool name alone is not an ABI guard.
    module = Path.home() / (
        "Library/Application Support/protonish/runners/"
        "highball-x64-crossover26.3-r28/root/lib/wine/x86_64-unix/winecoreaudio.so"
    )
    expected = "893be23ca5eefe4811c2732c4fa326fef2e0c5fc024264cf7db453365caf637d"
    if not module.is_file() or hashlib.sha256(module.read_bytes()).hexdigest() != expected:
        sys.exit("Unsupported winecoreaudio build; no files changed. See audio/README.md.")
    processes = subprocess.check_output(["/bin/ps", "-axo", "comm="], text=True)
    if any(line.strip().lower().endswith("/b1-win64-shipping.exe")
           for line in processes.splitlines()):
        sys.exit("Exit Wukong before replacing its audio component.")
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
    user_reg = steam / "steamapps/compatdata/2358720/pfx/user.reg"
    switch = "NOTPROTON_WUKONG_AUDIO_RT=1"
    plugin_text = (
        "[NOTPROTON_WUKONG_AUDIO_RT]\n"
        "label = Wine audio clock scheduling\n"
        "description = Real-time scheduling, buffer protection, and underrun de-popping for Wukong audio.\n"
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
            "-mmacosx-version-min=14.0", "-framework", "AudioToolbox",
            str(source), "-o", str(binary),
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

        if updated != text:
            processes = subprocess.check_output(["/bin/ps", "-axo", "comm="], text=True)
            if any(line.strip().endswith("/steam_osx") for line in processes.splitlines()):
                sys.exit("Exit native Steam before updating its per-game launch options.")

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
        if updated != text:
            atomic_write(path, updated.encode(), path.stat().st_mode & 0o777)

        if user_reg.exists():
            reg_text = user_reg.read_text(errors="surrogateescape")
            epic_key = "[Software\\\\Wine\\\\AppDefaults\\\\EpicWebHelper.exe\\\\DllOverrides]"
            if epic_key not in reg_text:
                shutil.copy2(user_reg, backup / "user.reg")
                epic_section = (
                    f"\n{epic_key} 1794079535\n"
                    "#time=1dd6eedc36a0ce4\n"
                    '"winegstreamer"=""\n'
                )
                marker = "[Software\\\\Wine\\\\AppDefaults]"
                idx = reg_text.find(marker)
                if idx != -1:
                    end_line = reg_text.find("\n\n", idx)
                    if end_line != -1:
                        reg_updated = reg_text[:end_line] + "\n" + epic_section + reg_text[end_line:]
                    else:
                        reg_updated = reg_text + epic_section
                else:
                    reg_updated = reg_text + epic_section
                atomic_write(
                    user_reg,
                    reg_updated.encode("utf-8", errors="surrogateescape"),
                    user_reg.stat().st_mode & 0o777,
                )

    print("Wukong audio clock fix installed.")
    print("Launch options", options)
    print("Backup", backup)


if __name__ == "__main__":
    main()
