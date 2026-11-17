#!/usr/bin/env python3
"""FH4-only diagnostic entry. Invoke its generated app using the computer UI.

This starts the owned executable with real package metadata; it neither supplies
an account license nor claims that Store activation/cloud saves are implemented.
"""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import threading


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def environment(profile):
    engine = Path(profile['engine'])
    graphics = Path(profile['graphicsEngine'])
    renderer = graphics / 'renderers/d3dmetal'
    shim = graphics / 'renderers/d3dmetal-tsshim'
    frameworks = graphics / 'frameworks'
    env = dict(os.environ)
    for key in tuple(env):
        if key.startswith(('WINE', 'DYLD_', 'FH4_', 'HB_', 'D3DM_', 'CX_', 'STEAM_', 'Steam')):
            env.pop(key)
    paths = ':'.join(map(str, [shim/'wine', renderer/'wine', engine/'engine/lib/wine']))
    env.update(WINEPREFIX=profile['prefix'], WINESERVER=str(engine/'engine/bin/wineserver'),
               WINEDEBUG='-all,+loaddll,+seh,+combase', WINEMSYNC='1', WINEESYNC='0',
               MVK_CONFIG_LOG_LEVEL='0', D3DM_MTL4='0',
               WINEDLLOVERRIDES='winemenubuilder.exe=d;d3d12,d3d12core,dxgi=n,b',
               WINEDLLPATH=paths, WINEDLLPATH_PREPEND=paths,
               CX_D3DMETALPATH=str(renderer/'external'),
               HB_D3D12_REAL='Z:'+str(shim/'wine/x86_64-windows/apd12.dll').replace('/', '\\'),
               DYLD_FALLBACK_FRAMEWORK_PATH=str(frameworks)+':/System/Library/Frameworks',
               DYLD_FALLBACK_LIBRARY_PATH=':'.join(map(str,[renderer/'external', frameworks, '/usr/lib'])))
    env.update(profile['packageEnvironment'])
    env.update(profile['graphicsEnvironment'])
    return env


def verify(profile):
    game = Path(profile['gameDirectory'])
    if game.name != 'Content' or game.parent.name != 'Forza-Horizon-4':
        raise RuntimeError('This entry is scoped to the owned FH4 Content directory')
    for item in profile['verifiedFiles']:
        if sha(item['path']) != item['sha256']:
            raise RuntimeError('Runtime changed; review before launching: '+item['path'])
    if not (Path(profile['prefix'])/'system.reg').is_file():
        raise RuntimeError('Prepared private prefix is missing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    profile = json.loads(args.profile.read_text())
    verify(profile)
    if args.check:
        print('Verified FH4-only entry; did not launch the game')
        return
    root = Path(profile.get('logDirectory', str(args.profile.parent)))
    root.mkdir(parents=True, exist_ok=True)
    with (root/'launch.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return
        now = datetime.datetime.now().astimezone().isoformat(timespec='seconds')
        stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
        log = root/('game-'+stamp+'.log')
        record = dict(started=now, game='FH4 Store', gameplayVerified=False, log=str(log))
        status = root/'current-run.json'
        with log.open('x') as output:
            proc = subprocess.Popen([str(Path(profile['engine'])/'engine/bin/wine'),
                                     str(Path(profile['gameDirectory'])/'ForzaHorizon4.exe')],
                                    env=environment(profile), cwd=profile['gameDirectory'],
                                    stdout=output, stderr=subprocess.STDOUT)
            record['winePid'] = proc.pid
            status.write_text(json.dumps(record, indent=2)+'\n')
            def snapshot():
                helper=profile.get('threadSnapshotHelper')
                if not helper or proc.poll() is not None:
                    return
                try:
                    with (root/('threads-'+stamp+'.log')).open('x') as trace:
                        subprocess.run([str(Path(profile['engine'])/'engine/bin/wine'),helper],
                                       env=environment(profile),stdout=trace,stderr=subprocess.STDOUT,timeout=15)
                except Exception as error:
                    (root/('threads-'+stamp+'.error')).write_text(type(error).__name__+'\n')
            timer=threading.Timer(10,snapshot);timer.daemon=True;timer.start()
            record['exitCode'] = proc.wait()
            timer.cancel()
        record['ended'] = datetime.datetime.now().astimezone().isoformat(timespec='seconds')
        status.write_text(json.dumps(record, indent=2)+'\n')


if __name__ == '__main__':
    main()
