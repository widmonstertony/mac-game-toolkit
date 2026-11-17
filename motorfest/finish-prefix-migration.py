#!/usr/bin/env python3
"""Finish only the installed Motorfest prefix migration without a Mono installer UI.

The mscoree override is confined to wineboot's environment. Normal game launches
retain CLR availability. This is container maintenance, not a game launch.
"""
import argparse, datetime, fcntl, hashlib, json, os, shutil, subprocess, time
from pathlib import Path

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def stop(server,env):
    # Wine returns 1 when there is no live server to kill. Still require the
    # scoped wait to finish successfully; do not ignore a timeout or other code.
    result=subprocess.run([str(server),'-k'],env=env,timeout=15)
    if result.returncode not in (0,1): result.check_returncode()
    subprocess.run([str(server),'-w'],env=env,check=True,timeout=15)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--proof', type=Path, required=True)
    p.add_argument('--apply', action='store_true')
    a=p.parse_args()
    data=Path.home()/'Library/Application Support/Steam/steamapps/compatdata/2698940'
    prefix=data/'pfx'; private=data/'.motorfest-private-20261117'
    profile=json.loads((private/'profile.json').read_text())
    installed=json.loads((private/'installed.json').read_text())
    engine=private/'MetalSharp-Wine.app/Contents'; candidate=private/'candidate'
    proof=json.loads(a.proof.read_text()); manifest=json.loads((candidate/'candidate.json').read_text())
    if (prefix.is_symlink() or profile['data']!=str(data) or not profile['enabled']
        or not installed['enabled'] or installed.get('rolledBack')): raise SystemExit('Active Motorfest profile required')
    if (not proof.get('passed') or not proof.get('nativeSteamShimChildVerified')
        or proof['candidateManifestSha256']!=sha(candidate/'candidate.json')
        or manifest['loaderSha256']!=sha(engine/'MacOS/wine')): raise SystemExit('Matched post-migration GPU/Steam proof required')
    for name,digest in proof['rendererFiles'].items():
        if sha(engine/name)!=digest: raise SystemExit('Renderer changed')
    print('Motorfest-only prefix update; mscoree disabled only during update; normal launcher unchanged.')
    if not a.apply: return
    env={k:v for k,v in os.environ.items() if not k.startswith(('WINE','DYLD_','CX_','FEX_','ROSETTA_','PROTON_','GRAPHICS_'))}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(engine/'lib/wine'),
        WINEDLLOVERRIDES='winemenubuilder.exe=d;mscoree=d', WINEDEBUG='-all,err+all')
    server=engine/'MacOS/wineserver'
    stop(server,env)
    with (data/'.notproton-prefix.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        backup=private/('migration-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
        backup.mkdir(mode=0o700)
        subprocess.run(['/bin/cp','-cR',str(prefix),str(backup/'before-pfx')],check=True)
        shutil.copy2(a.proof,backup/'isolated-post-update-proof.json')
        start=time.monotonic()
        try:
            with (backup/'wineboot.log').open('w') as log:
                rc=subprocess.run([str(engine/'MacOS/wine'),'wineboot','-u'],env=env,
                    stdout=log,stderr=subprocess.STDOUT,timeout=120).returncode
        finally:
            # Services from the update inherit its temporary overrides. End only
            # this prefix so the user's next Steam run starts with normal policy.
            stop(server,env)
        result=dict(exitCode=rc,elapsedSeconds=time.monotonic()-start,profileSha256=sha(private/'profile.json'),
            normalLauncherSha256=sha(private/'launch.py'),temporaryMonoOverride=True,
            persistentMonoOverride=False,gameLaunched=False,gameplayVerified=False)
        (backup/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        if rc: raise SystemExit('Update failed; preserved prefix backup and log: '+str(backup))
        print('Migration finished; next native Steam launch uses normal policy. Receipt:',backup/'result.json')

if __name__=='__main__': main()
