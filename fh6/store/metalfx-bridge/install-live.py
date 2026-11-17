#!/usr/bin/env python3
"""Install the verified MetalFX FG trial into the FH6-only runtime."""
import argparse, datetime, hashlib, json, os, shutil, subprocess, tempfile
from pathlib import Path

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def atomic(p,data):
    fd,tmp=tempfile.mkstemp(dir=p.parent,prefix='.'+p.name)
    try:
        with os.fdopen(fd,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
        os.replace(tmp,p)
    finally: Path(tmp).unlink(missing_ok=True)

def main():
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument('--artifacts',type=Path,required=True)
    a.add_argument('--carrier',type=Path,required=True)
    a.add_argument('--apply',action='store_true');a=a.parse_args()
    proof=json.loads((a.artifacts/'present-check/result.json').read_text())
    candidate=a.artifacts/'framegeneration-apple.dll'
    if not proof['passed'] or not proof['synthetic_scheduler_verified'] or not proof['shim_wrappers_enabled']:
        raise RuntimeError('Production-shim scheduler GPU verification required')
    if sha(candidate)!=proof['candidate_sha256']:raise RuntimeError('Candidate changed')
    root=Path.home()/'Library/Application Support/MACxus'
    runtime=root/'external-highball';profile=runtime/'fh6-profile.json'
    raw=profile.read_bytes();record=json.loads(raw)
    bottle=root/'bottles/xbox-highball'
    folder=bottle/'drive_c/highball/fg-native-ui'
    target=folder/'amd_fidelityfx_framegeneration_dx12.dll'
    observer=folder.parent/'store-plugins/fh6-fg-chain.asi'
    for p in [target,observer]:
        if sha(p)!=record['verifiedFiles'][str(p)]:raise RuntimeError('Unknown installed file: '+p.name)
    if sha(folder/'framegeneration-original.dll')!=proof['original_sha256']:raise RuntimeError('Original provider changed')
    if sha(bottle/'drive_c/windows/system32/d3d12.dll')!=proof['renderer_shim_sha256']:raise RuntimeError('Renderer changed')
    if sha(a.carrier/'x86_64-windows/nvngx.dll')!=proof['native_carrier_sha256']:raise RuntimeError('Carrier changed')
    game=Path(record['gamePath']);ini=game/'OptiScaler.ini';old_ini=ini.read_bytes()
    if b'FfxDx12FGPath = C:\\highball\\fg-native-ui' not in old_ini:
        raise RuntimeError('Unexpected FG path')
    wrapper=runtime/'fh6-store-wine';old_wrapper=wrapper.read_bytes()
    anchor=b'export WINEDLLPATH_PREPEND="$WINEDLLPATH"\n'
    if old_wrapper.count(anchor)!=1:raise RuntimeError('Unknown Wine wrapper')
    carrier=runtime/'fh6-metalfx-carrier'
    extra=(f'export WINEDLLPATH="{carrier}:$WINEDLLPATH"\n'
           'export WINEDLLPATH_PREPEND="$WINEDLLPATH"\n'
           'export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}nvngx=b"\n'
           'export D3DM_ENABLE_METALFX=1\n').encode()
    new_wrapper=old_wrapper.replace(anchor,anchor+extra)
    # This is an FG trial: SR remains explicitly labelled FSR until its live
    # command-list adapter exists. Unsupported FG frames retain the stock path.
    print(json.dumps(dict(operation='FH6 live MetalFX FG trial',upscaler=record['upscaler'],
                         candidateSha256=sha(candidate),liveGameVerified=False,apply=a.apply),indent=2))
    if not a.apply:return
    processes=subprocess.check_output(['/bin/ps','-axo','comm='],text=True).lower()
    if 'forzahorizon6.exe' in processes:raise RuntimeError('FH6 is running; stop its process before installation')
    stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    backup=runtime/('metalfx-backup-'+stamp);backup.mkdir()
    paths=[target,observer,wrapper,profile,ini]
    for i,p in enumerate(paths):shutil.copy2(p,backup/(str(i)+'-'+p.name))
    if carrier.exists():raise RuntimeError('Carrier already exists; review before reinstall')
    shutil.copytree(a.carrier,carrier,symlinks=False)
    if sha(carrier/'x86_64-windows/nvngx.dll')!=proof['native_carrier_sha256']:raise RuntimeError('Copied carrier mismatch')
    changes={target:candidate.read_bytes(),observer:(a.artifacts/'fh6-fg-chain.asi').read_bytes(),wrapper:new_wrapper}
    for p,data in changes.items():record['verifiedFiles'][str(p)]=hashlib.sha256(data).hexdigest()
    record['verifiedFiles'][str(carrier/'x86_64-windows/nvngx.dll')]=sha(carrier/'x86_64-windows/nvngx.dll')
    record['frameGeneration']='MetalFX FG candidate through DLSSG/FFX scheduler, 2x; stock fallback on warmup/unsupported frames; live validation pending'
    record['gameplayVerified']=False
    record['metalFxTrial']=dict(installed=True,fgInstalled=True,srInstalled=False,
        liveGameVerified=False,ghostingResolved=False,backup=str(backup),gpuProof=proof)
    record['sequenceCapture']['active']=False
    changes[profile]=(json.dumps(record,indent=2)+'\n').encode()
    try:
        for p,data in changes.items():atomic(p,data)
        for p,data in changes.items():
            if p.read_bytes()!=data:raise RuntimeError('Post-install mismatch')
        if ini.read_bytes()!=old_ini:raise RuntimeError('Game settings changed during install')
    except BaseException:
        for i,p in enumerate(paths):atomic(p,(backup/(str(i)+'-'+p.name)).read_bytes())
        raise
    (backup/'installation.json').write_text(json.dumps(record['metalFxTrial'],indent=2)+'\n')
    print('Installed FH6-only MetalFX FG trial; existing Steam entry retained. SR is still FSR Quality.')

if __name__=='__main__':main()
