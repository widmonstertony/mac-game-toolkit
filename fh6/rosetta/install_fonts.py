#!/usr/bin/env python3
"""Expose an existing macOS CJK font to FH6's private Wine prefix only."""
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    support = Path.home() / 'Library/Application Support/protonish/launchers/2483190'
    profile = support / 'rosetta-profile'
    font = Path('/Library/Fonts/Arial Unicode.ttf')
    if not font.is_file():
        font = Path('/System/Library/Fonts/Supplemental/Arial Unicode.ttf')
    if not font.is_file() or not (support / 'rosetta-profile-enabled').is_file():
        raise RuntimeError('Existing CJK font or FH6-only profile is missing')
    # Registering a font does not replace a mapped game DLL or alter a save.
    # Existing controls keep their selected HFONT until they are recreated.
    target = profile / 'prefix/drive_c/windows/Fonts/arial-unicode-local.ttf'
    if target.exists() and target.resolve() != font.resolve():
        raise RuntimeError('Another font already occupies this private-prefix path')
    if not target.is_symlink():
        target.symlink_to(font)
    spec = importlib.util.spec_from_file_location('fh6_profile', support / 'rosetta_profile.py')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    command = [str(profile / 'engine/bin/wine'), 'reg.exe', 'query',
               r'HKCU\Control Panel\Desktop\WindowMetrics', '/v', 'MessageFont']
    query = subprocess.run(command, env=m.environment(profile), check=True,
                           capture_output=True, text=True, timeout=30)
    match = re.search(r'MessageFont\s+REG_BINARY\s+([0-9a-fA-F]+)', query.stdout)
    if not match:
        raise RuntimeError('Could not read the actual Wine message-box font')
    previous = bytes.fromhex(match.group(1))
    if len(previous) != 92:
        raise RuntimeError('Unexpected LOGFONTW size; message font left unchanged')
    message_font = bytearray(previous)
    message_font[23] = 1  # DEFAULT_CHARSET; the chosen family also covers ANSI charset.
    message_font[28:] = 'Arial Unicode MS'.encode('utf-16-le').ljust(64, b'\0')
    # This is Wine's private registry, not macOS settings or any other game prefix.
    text = ('Windows Registry Editor Version 5.00\n\n'
            '[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts]\n'
            '"Arial Unicode MS (TrueType)"="arial-unicode-local.ttf"\n\n'
            '[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes]\n'
            '"MS Shell Dlg"="Arial Unicode MS"\n'
            '"MS Shell Dlg,0"="Arial Unicode MS"\n'
            '"MS Shell Dlg 2"="Arial Unicode MS"\n'
            '"MS Shell Dlg 2,0"="Arial Unicode MS"\n'
            '"Tahoma"="Arial Unicode MS"\n'
            '"Arial"="Arial Unicode MS"\n'
            '"Segoe UI"="Arial Unicode MS"\n'
            '"Microsoft Sans Serif"="Arial Unicode MS"\n'
            '"SimSun"="Arial Unicode MS"\n'
            '"Microsoft YaHei"="Arial Unicode MS"\n\n'
            '[HKEY_CURRENT_USER\\Control Panel\\Desktop\\WindowMetrics]\n'
            '"MessageFont"=hex:' + ','.join(f'{b:02x}' for b in message_font) + '\n')
    with tempfile.TemporaryDirectory(prefix='fh6-font-') as tmp:
        reg = Path(tmp) / 'fonts.reg'
        reg.write_text(text, encoding='utf-16')
        subprocess.run([str(profile / 'engine/bin/wine'), 'regedit.exe', '/S', str(reg)],
                       env=m.environment(profile), check=True, timeout=30)
    (profile / 'font-repair.json').write_text(json.dumps({
        'before_message_font': previous.hex(), 'after_message_font': message_font.hex(),
        'scope': 'FH6 private Wine message boxes only; host fonts/settings unchanged',
    }, indent=2) + '\n')
    print('Registered existing CJK font in the private FH6 prefix; no font redistributed.')


if __name__ == '__main__':
    main()
