#!/usr/bin/env python3
"""Query the owned package's factories in an isolated probe prefix, never a game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for name in ('candidate','engine','graphics-engine','prefix','probe','out'):
    p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
if a.prefix.name!='probe-prefix' or not (a.prefix/'system.reg').is_file():
    p.error('Requires an existing isolated probe-prefix')
data=json.loads((a.candidate/'candidate.json').read_text())
if data['package']['name']!='Microsoft.SunriseBaseGame' or data['installed']:
    p.error('Requires an inactive owned FH4 candidate')
game=Path(data['gameDirectory']).resolve()
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
if sha(game/'AppxManifest.xml')!=data['packageManifestSha256']:
    p.error('Package manifest changed')
sources=[]
for entry in data['files']:
    src=a.candidate/'files'/entry['file']
    if src.parent!=a.candidate/'files' or sha(src)!=entry['candidateSha256']:
        p.error('Candidate artifact changed')
    sources.append(src)
classes=[]
for server in data['packageClassServers']:
    dll=(game/server['file']).resolve()
    if dll.parent!=game or sha(dll)!=server['sha256']:
        p.error('Owned package server changed')
    classes.extend(server['classes'])
a.out.mkdir(parents=True,exist_ok=False)
for src in sources:shutil.copy2(src,a.out/src.name)
for server in data['packageClassServers']:
    (a.out/server['file']).symlink_to(game/server['file'])
shutil.copy2(a.probe,a.out/'class-probe.exe')
fw=a.graphics_engine.resolve()/'frameworks'
env=dict(os.environ,WINEPREFIX=str(a.prefix.resolve()),WINEDEBUG='-all,+input,+combase',
         WINEMSYNC='1',WINEESYNC='0',WINEDLLOVERRIDES='winemenubuilder.exe=d',
         DYLD_FALLBACK_LIBRARY_PATH=str(fw)+':/usr/lib',
         DYLD_FALLBACK_FRAMEWORK_PATH=str(fw)+':/System/Library/Frameworks')
def win(path):return 'Z:'+str(path.resolve()).replace('/','\\')
logpath=a.out/'run.log'
with logpath.open('w') as f:
    try:
        rc=subprocess.run([str(a.engine.resolve()/'engine/bin/wine'),str((a.out/'class-probe.exe').resolve()),
                           win(a.out/'ForzaHorizon4.exe.manifest'),win(game),*classes],
                          cwd=a.out,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=45).returncode
    except subprocess.TimeoutExpired:rc=124
log=logpath.read_text(errors='replace')
results=[dict(name=n,manifest=m=='1',hresult=h,factory=f=='1') for n,m,h,f in
         re.findall(r'CLASS name=(\S+) manifest=(\d) hr=([0-9a-f]+) factory=(\d)',log)]
verified=rc==0 and len(results)==len(classes) and all(x['manifest'] and x['factory'] and x['hresult']=='00000000' for x in results)
receipt=dict(exitCode=rc,allFactoriesVerified=verified,gameplayVerified=False,signInVerified=False,
             instancesCreated=0,probeSha256=sha(a.probe),logSha256=sha(logpath),results=results,
             sourceServers=data['packageClassServers'])
(a.out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(dict(exitCode=rc,requested=len(classes),returned=len(results),
                     factories=sum(x['factory'] for x in results),allFactoriesVerified=verified)))
raise SystemExit(0 if verified else 1)
