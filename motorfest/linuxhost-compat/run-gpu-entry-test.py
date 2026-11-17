#!/usr/bin/env python3
"""Run the real legacy DLL and an offscreen DX12 workload in an isolated prefix."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import secrets
import time

p=argparse.ArgumentParser(description=__doc__)
for key in ('candidate','engine','prefix','toolchain','out'):
    p.add_argument('--'+key,type=Path,required=True)
p.add_argument('--steam-entry',type=Path)
p.add_argument('--native-client',type=Path)
p.add_argument('--game-api',type=Path)
a=p.parse_args()
if any((a.steam_entry,a.native_client,a.game_api)) and not all((a.steam_entry,a.native_client,a.game_api)):
    raise SystemExit('Steam integration requires all three paths')
if a.prefix.resolve().name!='probe-prefix' or not (a.prefix/'system.reg').is_file():
    raise SystemExit('Requires an existing isolated probe-prefix')
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
candidate=a.candidate.resolve();engine=a.engine.resolve();out=a.out.resolve()
manifest=json.loads((candidate/'candidate.json').read_text())
if manifest['appId']!=2698940 or manifest['active']:
    raise SystemExit('Requires an inactive Motorfest candidate')
for name,digest in manifest['files'].items():
    if sha(candidate/name)!=digest:raise SystemExit('Candidate changed: '+name)
if sha(engine/'MacOS/wine')!=manifest['loaderSha256']:
    raise SystemExit('Engine changed')
# Wine's normal DLL lookup also needs a prefix-side registered file. An overlay
# path alone did not discover this new DLL in a separate application directory.
registered=a.prefix/'drive_c/windows/system32/beclient_x64.dll'
source_dll=candidate/'overlay/x86_64-windows/beclient_x64.dll'
if registered.is_symlink() or not registered.parent.resolve().is_relative_to(a.prefix.resolve()):
    raise SystemExit('Registered DLL path escapes isolated prefix')
if registered.exists() and sha(registered)!=sha(source_dll):
    raise SystemExit('Refuse existing different registered DLL')
shutil.copy2(source_dll,registered)
out.mkdir(parents=True,exist_ok=False)
source=Path(__file__).with_name('gpu-entry-probe.cpp')
cc=a.toolchain/'bin/x86_64-w64-mingw32-clang++'
subprocess.run([str(cc),'-O2','-Wall','-fno-exceptions','-fno-rtti','-static',str(source),
                '-ld3d12','-ldxgi','-o',str(out/'probe.exe')],check=True)
env={k:v for k,v in os.environ.items() if not k.startswith(('DYLD_','WINE','PROTON_','LINUXHOST_','GRAPHICS_','DXVK_','VKD3D_','CX_'))}
env.update(WINEPREFIX=str(a.prefix.resolve()),WINEDEBUG='-all,err+all,+loaddll,+module',
    WINEDLLOVERRIDES='winemenubuilder.exe=d;beclient_x64=b;d3d12,d3d12core,dxgi=b',
    WINEDLLPATH=':'.join(map(str,[candidate/'overlay/x86_64-windows',candidate/'overlay/aarch64-windows',candidate/'overlay',engine/'lib/mullion',engine/'lib/wine'])),
    LINUXHOST_LEGACY_HOST='Z:'+str(candidate/'host/linuxhost.dll').replace('/','\\'),
    LINUXHOST_LEGACY_BRIDGE=str(candidate/'runtime/v1/lib64/wine/beclient_x64.dll.so'),
    PROTON_BATTLEYE_RUNTIME=str(candidate/'runtime'),LINUXHOST_GNU_RUNTIME=str(candidate/'gnu'),
    LINUXHOST_NATIVE_HELPER=str(candidate/'host/native-child-helper'),
    LINUXHOST_AFFINITY_MODE='windows',GRAPHICS_BACKEND='mullion')
command=[str(engine/'MacOS/wine'),str(out/'probe.exe')]
if a.steam_entry:
    entry=a.steam_entry.resolve()
    build=json.loads((entry/'steam-entry-build.json').read_text())
    check=json.loads((entry/'entry-validation.json').read_text())
    if not check.get('childInitAndShutdownVerified') or not check.get('usedNativeSteamShim') or check['buildReceiptSha256']!=sha(entry/'steam-entry-build.json'):
        raise SystemExit('Verified Steam shim child receipt required')
    for name,digest in build['outputs'].items():
        if sha(entry/name)!=digest:raise SystemExit('Steam overlay changed: '+name)
    if sha(a.native_client)!=check['nativeClientSha256'] or sha(a.game_api)!=check['apiSha256']:
        raise SystemExit('Steam client or game API changed')
    if subprocess.run(['/usr/bin/pgrep','-x','steam_osx'],stdout=subprocess.DEVNULL).returncode:
        raise SystemExit('Native Steam is not running')
    target=a.prefix/'drive_c/Program Files (x86)/Steam'
    if not target.resolve().is_relative_to(a.prefix.resolve()) or target.is_symlink():raise SystemExit('Steam path escapes isolated prefix')
    target.mkdir(parents=True,exist_ok=True)
    for src in (entry/'steam').iterdir():
        dst=target/src.name
        if not src.is_file() or dst.is_symlink():raise SystemExit('Unexpected Steam bridge file')
        shutil.copy2(src,dst)
    nonce=secrets.token_hex(16);result_path=out/('child-result-'+nonce+'.txt')
    env.update(SteamAppId='2698940',SteamGameId='2698940',
        STEAM_COMPAT_CLIENT_INSTALL_PATH=str(a.native_client.resolve().parent),
        SteamPath='Z:'+str(entry/'steam').replace('/','\\'),
        MOTORFEST_STEAM_API='Z:'+str(a.game_api.resolve()).replace('/','\\'),
        MOTORFEST_PROBE_RESULT='Z:'+str(result_path).replace('/','\\'),MOTORFEST_PROBE_NONCE=nonce)
    env['WINEDLLPATH']=str(entry/'steam')+':'+str(entry/'overlay')+':'+env['WINEDLLPATH']
    env['WINEDLLOVERRIDES']+=';steamclient=n;steamclient64=n;lsteamclient=b'
    command.insert(1,str(entry/'steam/steam.exe'))
with (out/'run.log').open('w') as log:
    try:rc=subprocess.run(command,env=env,cwd=out,
                          stdout=log,stderr=subprocess.STDOUT,timeout=45).returncode
    except subprocess.TimeoutExpired:rc=124
text=(out/'run.log').read_text(errors='replace')
child_verified=False
if a.steam_entry:
    deadline=time.monotonic()+10
    while rc==0 and not result_path.exists() and time.monotonic()<deadline:time.sleep(.1)
    child_verified=result_path.is_file() and not result_path.is_symlink() and result_path.read_text()==f'STEAM_BRIDGE_GPU_AND_SHUTDOWN_OK {nonce}\n'
for line in text.splitlines():
    if any(s in line for s in ('BRIDGE_','GPU_','COMBINED_','err:')):print(line)
result={'exitCode':rc,'passed':rc==0 and (child_verified if a.steam_entry else 'COMBINED_BRIDGE_GPU_PASS' in text),
    'nativeSteamShimChildVerified':child_verified,
    'probeSha256':sha(out/'probe.exe'),'sourceSha256':sha(source),
    'runnerSha256':sha(Path(__file__)),'logSha256':sha(out/'run.log'),
    'candidateManifestSha256':sha(candidate/'candidate.json'),
    'registeredBridgeSha256':sha(registered),
    'rendererFiles':{str(f.relative_to(engine)):sha(f) for f in (engine/'lib/mullion').rglob('*') if f.is_file()},
    'vendorInitCalled':False,'visiblePresentationVerified':False,'gameplayVerified':False}
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('passed',result['passed'],'exitCode',rc)
raise SystemExit(0 if result['passed'] else 1)
