#!/usr/bin/env python3
"""Stage verified prior FH6 components without replacing the live Store prefix."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


FILES = ('d3d12.dll', 'd3d12core.dll', 'dxgi.dll', 'nvapi64.dll', 'ws2_32.dll')
STORE_VIEW_SHA = 'e568b6502cd84e7811a190c92a981b220e6e6e094b5b92762c67d93aea811589'
RUNTIME = ('engine/bin/wine', 'engine/bin/wineserver', 'engine/lib/wine/x86_64-windows/ntdll.dll',
           'engine/lib/wine/x86_64-unix/ntdll.so', 'engine/lib/wine/x86_64-windows/kernelbase.dll')


def sha(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def stage(build, out):
    home = Path.home()
    old = home/'Library/Application Support/protonish/launchers/2483190/rosetta-profile'
    validation=json.loads((old/'validation.json').read_text())
    if not validation['compatible']:
        raise RuntimeError('Need the previously validated private Wine components')
    renderer=json.loads((old/'renderer-files.json').read_text())
    out.mkdir(parents=True,exist_ok=False)
    shutil.copytree(old/'engine',out/'engine',symlinks=True)
    config=json.loads((home/'Library/Application Support/MACxus/local-highball.json').read_text())
    (out/'frameworks').symlink_to(Path(config['engineRoot'])/'frameworks',target_is_directory=True)
    patch_target=out/'engine/lib/wine/x86_64-windows/ntdll.dll'
    # New PE policy guard paired with the already verified Highball Unix ABI.
    patch_target.unlink()
    shutil.copy2(build/'dlls/ntdll/x86_64-windows/ntdll.dll',patch_target)
    checked = {str(p.relative_to(out)):sha(p) for p in
               (patch_target,out/'engine/bin/wine',out/'engine/bin/wineserver')}
    for relative in ('engine/lib/wine/x86_64-unix/ntdll.so','engine/lib/wine/x86_64-windows/kernelbase.dll'):
        if sha(out/relative)!=validation['files'][relative]:
            raise RuntimeError('Unverified companion module: '+relative)
        checked[relative]=validation['files'][relative]
    # The Steam engine's old winhttp is deliberately not used. Store GDK
    # requires the official Nexus native HTTP/2 module in its own prefix.
    httppath=out/'engine/lib/wine/x86_64-windows/winhttp.dll'
    httppath.unlink()
    httppath.symlink_to(Path(config['engineRoot'])/'engine/lib/wine/x86_64-windows/winhttp.dll')
    (out/'system32').mkdir()
    for name in FILES:
        relative='prefix/drive_c/windows/system32/'+name
        if name=='ws2_32.dll':relative='engine/lib/wine/x86_64-windows/ws2_32.dll'
        source=old/relative
        expected=renderer.get(relative,validation['files'].get(relative))
        if not expected or sha(source)!=expected:raise RuntimeError('Unverified prior component: '+name)
        shutil.copy2(source,out/'system32'/name)
        checked['system32/'+name]=expected
    record={'schema':1,'storeViewSha256':STORE_VIEW_SHA,'files':checked,
            'passed':False,'gameplayVerified':False,'scope':'Store-only CPU, renderer, NvAPI and dual-stack UDP staged; retain Nexus HTTP/2'}
    (out/'prior-fixes.json').write_text(json.dumps(record,indent=2)+'\n')
    print('Private engine and verified five DLLs staged; execution validation still required.')


def validate(out, probes):
    config=json.loads((Path.home()/'Library/Application Support/MACxus/local-highball.json').read_text())
    engine=out/'engine';prefix=out.parent/'probe-prefix';render=Path(config['rendererRoot']);shim=Path(config['shimRoot']);frameworks=out/'frameworks'
    paths=':'.join(map(str,[shim/'wine',render/'wine',engine/'lib/wine']))
    env=dict(os.environ,WINEPREFIX=str(prefix),WINEDEBUG='-all',WINEMSYNC='1',WINEESYNC='0',D3DM_MTL4='0',
             D3DM_VENDOR_ID='0x10de',D3DM_DEVICE_ID='0x2786',D3DM_DEVICE_DESCRIPTION='NVIDIA GeForce RTX 4070',
             CX_D3DMETALPATH=str(render/'external'),
             DYLD_FALLBACK_LIBRARY_PATH=':'.join(map(str,[render/'external',frameworks,frameworks/'GStreamer.framework/Versions/1.0/lib','/usr/lib'])),
             DYLD_FALLBACK_FRAMEWORK_PATH=str(frameworks)+':/System/Library/Frameworks',
             WINEDLLPATH=paths,WINEDLLPATH_PREPEND=paths,
             WINEDLLOVERRIDES='d3d12,d3d12core,dxgi,nvapi64=n,b;winemenubuilder.exe=d;winhttp=n,b',
             HB_D3D12_REAL='Z:'+str(shim/'wine/x86_64-windows/apd12.dll').replace('/','\\'))
    results=[]
    def run(name,args,extra=None):
        log=out.parent/f'{name}.log'
        with log.open('w') as f:
            result=subprocess.run([str(engine/'bin/wine'),*map(str,args)],cwd=probes,env={**env,**(extra or {})},
                                  stdout=f,stderr=subprocess.STDOUT,timeout=90)
        lines=log.read_text(errors='replace').splitlines()
        results.append({'test':name,'code':result.returncode,'tail':lines[-3:]})
        print(name,result.returncode,'\n'.join(lines[-3:]),flush=True)
        if result.returncode:raise RuntimeError(name+' failed')
    run('prefix',['wineboot','-u'])
    for file in (out/'system32').iterdir():shutil.copy2(file,prefix/'drive_c/windows/system32'/file.name)
    run('exception',[probes/'exception_chain_probe.exe','enabled-clean'],
        {'WINE_SOFTWARE_EXCEPTION_CHAIN':'probe-clean','WINE_SOFTWARE_EXCEPTION_STATS':'1','WINE_SOFTWARE_BRIDGE_RESUME':'1','WINE_EXCEPTION_BURST_AUTO':'probe'})
    run('gpu',[probes/'store-environment-probe.exe'],{'WINEDEBUG':'-all,+loaddll'})
    if 'apd12.dll' not in (out.parent/'gpu.log').read_text():
        raise RuntimeError('D3DMetal was not loaded')
    run('audio',[probes/'audio-probe.exe'],{'DYLD_INSERT_LIBRARIES':str(frameworks/'libhbaudiobuf.dylib')})
    run('nvapi',[probes/'nvapi-probe.exe'])
    run('udp',[probes/'store-network-probe.exe','--udp-only'])
    (out.parent/'probes.json').write_text(json.dumps(results,indent=2)+'\n')
    receipt=out/'prior-fixes.json';record=json.loads(receipt.read_text())
    for relative in ('engine/bin/wine','engine/bin/wineserver'):
        record['files'][relative] = sha(out/relative)
    record.update(passed=True,verifiedProbes=[r['test'] for r in results]);receipt.write_text(json.dumps(record,indent=2)+'\n')


def replace_file(src, dst):
    fd, temporary = tempfile.mkstemp(prefix='.' + dst.name + '.', dir=dst.parent)
    os.close(fd)
    try:
        shutil.copy2(src, temporary)
        os.replace(temporary, dst)
    finally:
        Path(temporary).unlink(missing_ok=True)


def install(layer):
    """Install only the validated next-launch layer; do not start FH6."""
    data = Path.home()/'Library/Application Support/MACxus'
    root = data/'external-highball'
    system32 = data/'bottles/xbox-highball/drive_c/windows/system32'
    dest = root/'fh6-prior-fixes'
    incoming = root/'.fh6-prior-fixes-incoming'
    if dest.exists() or incoming.exists():
        raise RuntimeError('An FH6 layer already exists; review its receipt before replacing it')
    receipt = json.loads((layer/'prior-fixes.json').read_text())
    if (receipt.get('passed') is not True or receipt.get('storeViewSha256') != STORE_VIEW_SHA or
        set(receipt['verifiedProbes']) != {'prefix','exception','gpu','audio','nvapi','udp'}):
        raise RuntimeError('Validate the full private layer first')
    for relative in (*RUNTIME, *('system32/'+f for f in FILES)):
        if sha(layer/relative) != receipt['files'].get(relative):
            raise RuntimeError('Layer changed after validation: '+relative)
    ps = subprocess.check_output(['/bin/ps','-axo','comm='], text=True)
    if 'forzahorizon6.exe' in ps.lower():
        raise RuntimeError('Exit the active FH6 game normally before applying')
    preserved = ('winhttp.dll', 'xgameruntime.dll')
    retained = {name:sha(system32/name) for name in preserved}
    receipt['retainedNexusFiles'] = retained
    # A short-lived rollback buffer prevents leaving a mix of DLLs after a write
    # error. The active launcher is swapped last; no second runtime is started.
    shutil.copytree(layer,incoming,symlinks=True)
    with tempfile.TemporaryDirectory(dir=root) as temp:
        prev = Path(temp)
        changed = []
        scripts = ('local-highball.js','runner.mjs')
        try:
            for name in scripts: shutil.copy2(root/name,prev/name)
            for name in FILES:
                target = system32/name
                shutil.copy2(target,prev/name)
                replace_file(incoming/'system32'/name,target)
                changed.append(name)
                if sha(target) != receipt['files']['system32/'+name]:
                    raise RuntimeError('Could not verify installed '+name)
            if any(sha(system32/name) != status for name,status in retained.items()):
                raise RuntimeError('Existing Nexus module unexpectedly changed')
            (incoming/'prior-fixes.json').write_text(json.dumps(receipt,indent=2)+'\n')
            incoming.rename(dest)
            for name in scripts: replace_file(Path(__file__).with_name(name),root/name)
        except BaseException:
            for name in scripts:
                if (prev/name).is_file(): replace_file(prev/name,root/name)
            for name in changed:
                replace_file(prev/name,system32/name)
            if dest.exists(): shutil.rmtree(dest)
            if incoming.exists(): shutil.rmtree(incoming)
            raise
    print('Verified Store-only files installed for the next launch; original Nexus modules match.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);p.add_argument('out',type=Path)
    group=p.add_mutually_exclusive_group()
    group.add_argument('--validate',type=Path,help='Run existing owned probes in isolated prefix; argument is probe directory')
    group.add_argument('--install',action='store_true',help='Install the validated output for the next Store launch')
    a=p.parse_args()
    if a.validate:validate(a.out.resolve(),a.validate.resolve())
    elif a.install:install(a.out.resolve())
    else:stage(a.build.resolve(),a.out.resolve())
