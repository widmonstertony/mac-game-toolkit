#!/usr/bin/env python3
"""Expose an existing macOS CJK font to FH6's private Wine prefix only."""
import importlib.util
from pathlib import Path
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
    if target.exists() and target.resolve() != font:
        raise RuntimeError('Another font already occupies this private-prefix path')
    if not target.is_symlink():
        target.symlink_to(font)
    spec = importlib.util.spec_from_file_location('fh6_profile', support / 'rosetta_profile.py')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
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
            '"Microsoft YaHei"="Arial Unicode MS"\n')
    with tempfile.TemporaryDirectory(prefix='fh6-font-') as tmp:
        reg = Path(tmp) / 'fonts.reg'
        reg.write_text(text, encoding='utf-16')
        subprocess.run([str(profile / 'engine/bin/wine'), 'regedit.exe', '/S', str(reg)],
                       env=m.environment(profile), check=True, timeout=30)
    print('Registered existing CJK font in the private FH6 prefix; no font redistributed.')


if __name__ == '__main__':
    main()
