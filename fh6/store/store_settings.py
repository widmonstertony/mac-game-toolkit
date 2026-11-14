"""Update the active Xbox graphics preferences and leave save data alone."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from prior_fixes import replace_file


PROFILE_DIRECTORY = Path('AppData/Local/XodusPersistentLocalStorage') / 'Microsoft.ForteBaseGame_8wekyb3d8bbwe'
SETTINGS_FILE = Path('LocalStorage_Shared/ForzaUserConfigSelections/UserConfigSelections')


def set_log_level(ini):
    raw = ini.read_bytes()
    lines = raw.splitlines(keepends=True)
    in_log_section = False
    changed = 0
    for index, line in enumerate(lines):
        if line.startswith(b'['):
            in_log_section = line.strip() == b'[Log]'
        if in_log_section and line.partition(b'=')[0].strip() == b'LogLevel':
            ending = line[len(line.rstrip(b'\r\n')):]
            lines[index] = b'LogLevel = 3' + ending
            changed += 1
    if changed != 1:
        raise RuntimeError('Expected a single OptiScaler file log level')
    return b''.join(lines)


def set_playback(settings_file):
    parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
    tree = ET.parse(settings_file, parser=parser)
    root = tree.getroot()
    if root.tag != 'UserConfig' or root.get('Version') != '52':
        raise RuntimeError('Unexpected FH6 graphics settings format')
    settings, selections = root.find('settings'), root.find('selections')
    if (settings is None or selections is None or
        any(settings.find(name) is None or settings.find(name).get('value') != value
            for name, value in [('ResolutionWidth', '3472'), ('ResolutionHeight', '2266')])):
        raise RuntimeError('Unexpected FH6 output resolution')
    mode = selections.findall("./*[@id='DLSSMode']")
    if len(mode) != 1 or mode[0].get('value') != '1':
        raise RuntimeError('Expected the selected FSR upscaling input and its frame generation option')
    # Match the last usable Steam profile's presentation choices. Leave the
    # engine's present interval and the Xbox picture/detail options intact.
    for name, value in {'DLSSGMode': '2', 'FrameRate': '4', 'VSync': '0'}.items():
        matches = selections.findall(f"./*[@id='{name}']")
        if len(matches) != 1:
            raise RuntimeError('Missing or ambiguous playback selection: ' + name)
        matches[0].set('value', value)
    tearing = settings.findall('AllowScreenTearing')
    if len(tearing) != 1:
        raise RuntimeError('Missing or ambiguous tearing preference')
    tearing[0].set('value', '1')
    return ET.tostring(root, encoding='utf-8') + b'\n'


def update(apply):
    data = Path.home() / 'Library/Application Support/MACxus'
    prefix = data / 'bottles/xbox-highball'
    paths = list((prefix / 'drive_c/users').glob(str(Path('*') / PROFILE_DIRECTORY / SETTINGS_FILE)))
    if len(paths) != 1 or not paths[0].is_file():
        raise RuntimeError('Expected one existing Xbox FH6 graphics settings file')
    profile = json.loads((data / 'external-highball/fh6-profile.json').read_text())
    game = Path(profile['gamePath'])
    if not game.is_absolute() or not (game / 'MicrosoftGame.config').is_file():
        raise RuntimeError('Expected the configured FH6 Store package')
    ini = game / 'OptiScaler.ini'
    changes = [(ini, set_log_level(ini)), (paths[0], set_playback(paths[0]))]
    if apply:
        running = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True)
        if 'forzahorizon6.exe' in running.lower():
            raise RuntimeError('The active game can rewrite these graphics preferences on exit')
        with tempfile.TemporaryDirectory() as directory:
            staged = Path(directory)
            for index, (target, content) in enumerate(changes):
                original = staged / f'{index}.orig'
                original.write_bytes(target.read_bytes())
                candidate = staged / str(index)
                candidate.write_bytes(content)
                candidate.chmod(target.stat().st_mode & 0o777)
                original.chmod(candidate.stat().st_mode & 0o777)
            committed = []
            try:
                for index, (target, content) in enumerate(changes):
                    if target.read_bytes() != content:
                        replace_file(staged / str(index), target)
                        committed.append((index, target))
            except BaseException:
                for index, target in reversed(committed):
                    replace_file(staged / f'{index}.orig', target)
                raise
    print(f'{"Applied" if apply else "Validated"} 3472 × 2266 with 2× FG, the Steam playback preferences and warning-level logging.')


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--apply', action='store_true')
    update(cli.parse_args().apply)
