#!/usr/bin/env python3
"""Test browser D3D11 resource sharing without launching Ubisoft or a game."""
import argparse, hashlib, json, os, secrets, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for n in ('engine','prefix','toolchain','out'):p.add_argument('--'+n,type=Path,required=True)
p.add_argument('--browser-dll',type=Path,help='Negative/control route: existing browser-local D3D11')
a=p.parse_args()
if a.prefix.resolve().name!='probe-prefix' or not (a.prefix/'system.reg').is_file():raise SystemExit('Isolated existing probe-prefix required')
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
source=Path(__file__).with_name('browser-gpu-probe.cpp')
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
subprocess.run([str(a.toolchain/'bin/x86_64-w64-mingw32-clang++'),'-O2','-Wall','-static','-municode',str(source),'-o',str(out/'probe.exe')],check=True)
engine=a.engine.resolve()
env={k:v for k,v in os.environ.items() if not k.startswith(('DYLD_','WINE','PROTON_','LINUXHOST_','GRAPHICS_','DXVK_','VKD3D_','CX_'))}
env.update(WINEPREFIX=str(a.prefix.resolve()),WINEDEBUG='-all,err+all,+loaddll',
    WINEDLLPATH=str(engine/'lib/mullion')+':'+str(engine/'lib/wine'),
    WINEDLLOVERRIDES='winemenubuilder.exe=d;d3d11,dxgi='+('n,b' if a.browser_dll else 'b'),
    GRAPHICS_BACKEND='mullion',DXMT_ALLOW_CROSS_PROCESS_SWAPCHAIN='1',WINE_SIMULATE_WRITECOPY='1')
dll='Z:'+str(a.browser_dll.resolve()).replace('/','\\') if a.browser_dll else 'd3d11.dll'
with (out/'run.log').open('w') as log:
    try:rc=subprocess.run([str(engine/'MacOS/wine'),str(out/'probe.exe'),dll,'Local\\MotorfestBrowserProbe'+secrets.token_hex(16)],
        cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=45).returncode
    except subprocess.TimeoutExpired:rc=124
log=(out/'run.log').read_text(errors='replace')
passed=rc==0 and 'BROWSER_CROSS_PROCESS_GPU_PASS' in log
result=dict(passed=passed,exitCode=rc,sourceSha256=sha(source),probeSha256=sha(out/'probe.exe'),
    logSha256=sha(out/'run.log'),loaderSha256=sha(engine/'MacOS/wine'),
    browserLocalControl=bool(a.browser_dll),browserDllSha256=sha(a.browser_dll) if a.browser_dll else None,
    rendererFiles={str(f.relative_to(engine)):sha(f) for f in (engine/'lib/mullion').rglob('*') if f.is_file()},
    visiblePresentationVerified=False,ubisoftStarted=False,gameplayVerified=False)
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('\n'.join(x for x in log.splitlines() if 'BROWSER_' in x or 'err:' in x)[-4000:])
print('passed',passed,'exitCode',rc)
raise SystemExit(0 if passed else 1)
