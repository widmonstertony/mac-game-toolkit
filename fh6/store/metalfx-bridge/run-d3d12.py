#!/usr/bin/env python3
"""Run a headless GPU probe in an existing, explicitly selected test prefix.

Never installs a DLL in a game or touches game settings. Requires the local
Highball configuration used by this project. No account data is printed.
"""
import argparse,json,os,subprocess
from pathlib import Path

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--test-prefix',type=Path,required=True)
    parser.add_argument('--upscale-chain',action='store_true')
    a=parser.parse_args();build=a.build.resolve();prefix=a.test_prefix.resolve()
    probe='test-upscale-d3d12' if a.upscale_chain else 'test-d3d12'
    data=Path.home()/'Library/Application Support/MACxus'
    if prefix==data/'bottles' or data.joinpath('bottles').resolve() in prefix.parents:
        raise SystemExit('Refusing a live MACxus bottle; select an existing isolated test prefix.')
    if not (prefix/'system.reg').is_file():raise SystemExit('Existing test prefix required.')
    for relative in (probe+'.exe','x86_64-windows/fh6metalfx.dll','x86_64-unix/fh6metalfx.so'):
        if not (build/relative).is_file():raise SystemExit('Missing build artifact: '+relative)
    cfg=json.loads((data/'local-highball.json').read_text())
    layer=data/'external-highball/fh6-prior-fixes'
    renderer,shim=Path(cfg['rendererRoot']),Path(cfg['shimRoot'])
    frameworks=Path(cfg['engineRoot'])/'frameworks'
    dlls=':'.join(map(str,(build,shim/'wine',renderer/'wine',layer/'engine/lib/wine')))
    libraries=':'.join(map(str,(renderer/'external',frameworks,frameworks/'GStreamer.framework/Versions/1.0/lib',Path(cfg['libraryRoot']))))
    win=lambda p:'Z:'+str(p).replace('/','\\')
    env=dict(os.environ)
    env.update(WINEPREFIX=str(prefix),WINEDLLOVERRIDES='fh6metalfx=b;winemenubuilder.exe=d;d3d12,d3d12core,dxgi=n,b',
        WINEDLLPATH=dlls,WINEDLLPATH_PREPEND=dlls,DYLD_FALLBACK_LIBRARY_PATH=libraries,
        DYLD_FALLBACK_FRAMEWORK_PATH=str(frameworks)+':/System/Library/Frameworks',
        WINEMSYNC='1',WINEESYNC='0',CX_D3DMETALPATH=str(renderer/'external'),D3DM_MTL4='0',
        HB_D3D12_REAL=win(shim/'wine/x86_64-windows/apd12.dll'),WINEDEBUG='-all',FH6_D3DMETAL_NO_ANTILAG2='1')
    log=build/(probe+'.log')
    with log.open('w') as stream:
        try:rc=subprocess.run([str(layer/'engine/bin/wine'),str(build/(probe+'.exe'))],cwd=build,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=90).returncode
        except subprocess.TimeoutExpired:rc=124
    lines=log.read_text(errors='replace').splitlines()
    for line in lines:
        if line.startswith(('caps=','module=','device_method=','mapped ','dispatch=','D3D12 readback','FAIL','PASS')):print(line)
    print('exit',rc,'log',log)
    raise SystemExit(rc)
if __name__=='__main__':main()
