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
    a.add_argument('--keep-observer',action='store_true',help='Preserve the verified native Streamline observer')
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
    carrier=runtime/'fh6-metalfx-carrier'
    source_so=a.carrier/'x86_64-unix/nvngx.so'
    if not source_so.is_symlink() or not source_so.resolve().is_file():
        raise RuntimeError('Use the validated carrier link to the existing Apple library')
    if carrier.exists() and sha(carrier/'x86_64-windows/nvngx.dll')!=proof['native_carrier_sha256']:
        raise RuntimeError('Existing carrier differs; review before upgrade')
    # The runner regenerates the wrapper on EVERY launch. Install its source
    # policy too; editing only the generated file makes integrity checks fail.
    source=Path(__file__).resolve().parents[1]
    generator_files={runtime/n:(source/n).read_bytes() for n in ('local-highball.js','runner.mjs')}
    # This is an FG trial: SR remains explicitly labelled FSR until its live
    # command-list adapter exists. Unsupported FG frames retain the stock path.
    print(json.dumps(dict(operation='FH6 live MetalFX FG trial',upscaler=record['upscaler'],
                         candidateSha256=sha(candidate),liveGameVerified=False,apply=a.apply),indent=2))
    if not a.apply:return
    processes=subprocess.check_output(['/bin/ps','-axo','comm='],text=True).lower()
    if 'forzahorizon6.exe' in processes:raise RuntimeError('FH6 is running; stop its process before installation')
    stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    backup=runtime/('metalfx-backup-'+stamp);backup.mkdir()
    paths=[target,observer,wrapper,profile,ini,*generator_files]
    for i,p in enumerate(paths):shutil.copy2(p,backup/(str(i)+'-'+p.name))
    if not carrier.exists():shutil.copytree(a.carrier,carrier,symlinks=True)
    installed_so=carrier/'x86_64-unix/nvngx.so'
    if sha(installed_so)!=sha(source_so):raise RuntimeError('Native carrier changed')
    # dlopen identity matters as well as bytes. Dereferencing this link loads a
    # second libd3dshared image alongside D3DMetal and fails its initialization.
    if not installed_so.is_symlink() or installed_so.resolve()!=source_so.resolve():
        shutil.copy2(installed_so,backup/'nvngx-so-before-link-repair')
        link=installed_so.with_name('.nvngx-link-staged')
        link.symlink_to(source_so.resolve());os.replace(link,installed_so)
    if sha(carrier/'x86_64-windows/nvngx.dll')!=proof['native_carrier_sha256']:raise RuntimeError('Copied carrier mismatch')
    changes={target:candidate.read_bytes(),**generator_files}
    if not a.keep_observer:
        changes[observer]=(a.artifacts/'fh6-fg-chain.asi').read_bytes()
    for p,data in changes.items():record['verifiedFiles'][str(p)]=hashlib.sha256(data).hexdigest()
    record['verifiedFiles'][str(carrier/'x86_64-windows/nvngx.dll')]=sha(carrier/'x86_64-windows/nvngx.dll')
    record['verifiedFiles'][str(installed_so)]=sha(installed_so)
    record['frameGeneration']='MetalFX FG candidate through DLSSG/FFX scheduler, 2x; stock fallback on warmup/unsupported frames; live validation pending'
    record['gameplayVerified']=False
    record['metalFxTrial']=dict(installed=True,fgInstalled=True,srInstalled=False,
        liveGameVerified=False,ghostingResolved=False,backup=str(backup),gpuProof=proof)
    record['sequenceCapture']['active']=False
    changes[profile]=(json.dumps(record,indent=2)+'\n').encode()
    try:
        for p,data in changes.items():atomic(p,data)
        # 'check' performs the identical runner generation and validation, but
        # never starts Xodus, account authentication, Wine or the game image.
        node=Path.home()/'.nvm-arm64/versions/node/v26.12.1/bin/node'
        first=subprocess.run([str(node),str(runtime/'runner.mjs'),'check'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if first.returncode and b'component changed; reconfigure before launch: fh6-store-wine' not in first.stdout:
            raise RuntimeError('Entry generation failed: '+first.stdout.decode(errors='replace')[-800:])
        generated=wrapper.read_bytes()
        if not all(x in generated for x in (str(carrier).encode(),b'nvngx=b',b'D3DM_ENABLE_METALFX=1')):
            raise RuntimeError('Generated runner omitted MetalFX policy')
        record['verifiedFiles'][str(wrapper)]=hashlib.sha256(generated).hexdigest()
        changes[profile]=(json.dumps(record,indent=2)+'\n').encode()
        atomic(profile,changes[profile])
        subprocess.run([str(node),str(runtime/'runner.mjs'),'check'],stdout=subprocess.DEVNULL,check=True)
        for p,data in changes.items():
            if p.read_bytes()!=data:raise RuntimeError('Post-install mismatch')
        if ini.read_bytes()!=old_ini:raise RuntimeError('Game settings changed during install')
    except BaseException:
        for i,p in enumerate(paths):atomic(p,(backup/(str(i)+'-'+p.name)).read_bytes())
        previous_native=backup/'nvngx-so-before-link-repair'
        if previous_native.exists():atomic(installed_so,previous_native.read_bytes())
        raise
    (backup/'installation.json').write_text(json.dumps(record['metalFxTrial'],indent=2)+'\n')
    print('Installed FH6-only MetalFX FG trial; existing Steam entry retained. SR is still FSR Quality.')

if __name__=='__main__':main()
