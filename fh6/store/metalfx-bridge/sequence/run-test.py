#!/usr/bin/env python3
"""Compare the exact installed FH6 provider with an outer, explicit recorder.

Only copies private DLLs into the selected build folder and uses an existing
isolated Wine prefix. Does not install, start or stop a game.
"""
import argparse, hashlib, json, os, shutil, struct, subprocess
from pathlib import Path

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def verify_sequence(folder,build):
    summary=json.loads((folder/'sequence.json').read_text())
    if summary!={'complete':True,'frames':8,'first':1,'last':8}:raise RuntimeError('Incomplete/nonconsecutive fixture sequence')
    checked=0
    for frame in range(1,9):
        root=folder/f'frame-{frame}';meta=json.loads((root/'capture.json').read_text())
        native=(root/'native-frame.bin').read_bytes()
        if len(native)!=608 or struct.unpack_from('<Q',native)[0]!=frame or meta['frame']!=frame or not meta['gpu_fence_completed'] or not meta['pixel_files_complete']:raise RuntimeError('Frame/fence mismatch')
        images={i['name']:i for i in meta['images']}
        if set(images)!={'prepare-current-depth','prepare-current-motion','scene','generated-after-ui'}:raise RuntimeError('Image set mismatch')
        for name,info in images.items():
            raw=(root/(name+'.bin')).read_bytes()
            w,h,pitch=info['width'],info['height'],info['row_pitch']
            if (w,h)!=(128,96) or info['source_frame']!=frame or len(raw)!=info['bytes'] or len(raw)!=h*pitch:raise RuntimeError('Image layout mismatch')
            tight=b''.join(raw[y*pitch:y*pitch+w*4] for y in range(h))
            if name=='generated-after-ui':
                if tight!=(build/f'expected-frame-{frame}.rgba').read_bytes():raise RuntimeError('Serialized output differs from independent GPU readback')
                checked+=w*h;continue
            for y in range(h):
                for x in range(w):
                    car=20+frame*2<=x<52+frame*2 and 40<=y<72;off=4*(y*w+x)
                    if name=='scene':expected=bytes((100,30,20,255) if car else (25,40,60,255))
                    elif name=='prepare-current-depth':expected=struct.pack('<f',.8 if car else .2)
                    else:expected=struct.pack('<HH',0xc000 if car else 0,0)
                    if tight[off:off+4]!=expected:raise RuntimeError(f'Wrong epoch/pixel: frame={frame} image={name} x={x} y={y}')
                    checked+=1
    return checked

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--test-prefix',type=Path,required=True);p.add_argument('--fixtures',type=Path,required=True)
    a=p.parse_args();build=a.build.resolve();prefix=a.test_prefix.resolve();data=Path.home()/'Library/Application Support/MACxus'
    if data.resolve() in build.parents or data.resolve() in prefix.parents or not (prefix/'system.reg').is_file():raise SystemExit('Require an isolated build directory and existing test prefix outside MACxus')
    manifest=json.loads((build/'build.json').read_text())
    for name,digest in manifest['artifacts'].items():
        if sha(build/name)!=digest:raise RuntimeError('Changed build artifact '+name)
    installed=data/'bottles/xbox-highball/drive_c/highball'
    sources={'framegeneration-sequence-inner.dll':installed/'fg-native-ui/amd_fidelityfx_framegeneration_dx12.dll',
             'framegeneration-source.dll':installed/'fg-native-ui/framegeneration-source.dll',
             'framegeneration-original.dll':installed/'fg-native-ui/framegeneration-original.dll',
             'fh6-fg-chain.asi':installed/'store-plugins/fh6-fg-chain.asi',
             'sl.interposer.dll':a.fixtures.resolve()/'sl.interposer.dll'}
    inner=installed/'fg-native-ui/framegeneration-sequence-inner.dll'
    if inner.exists():
        profile=json.loads((data/'external-highball/fh6-profile.json').read_text())
        for path in (inner,sources['framegeneration-sequence-inner.dll']):
            if path.is_symlink() or profile['verifiedFiles'].get(str(path))!=sha(path):raise RuntimeError('Installed recorder/input hash mismatch')
        if profile.get('sequenceCapture',{}).get('innerSha256')!=sha(inner):raise RuntimeError('Unverified inner provider')
        sources['framegeneration-sequence-inner.dll']=inner
    private_hashes={name:sha(src) for name,src in sources.items()}
    for name,src in sources.items():shutil.copy2(src,build/name)
    cfg=json.loads((data/'local-highball.json').read_text());layer=data/'external-highball/fh6-prior-fixes'
    renderer,shim=Path(cfg['rendererRoot']),Path(cfg['shimRoot']);frameworks=Path(cfg['engineRoot'])/'frameworks'
    dlls=':'.join(map(str,(shim/'wine',renderer/'wine',layer/'engine/lib/wine')))
    libs=':'.join(map(str,(renderer/'external',frameworks,frameworks/'GStreamer.framework/Versions/1.0/lib',Path(cfg['libraryRoot']))))
    win=lambda p:'Z:'+str(p).replace('/','\\')
    env={k:v for k,v in os.environ.items() if not k.startswith(('XODUS_','XUSER_','NEXUS_'))}
    env.update(WINEPREFIX=str(prefix),WINEDLLOVERRIDES='winemenubuilder.exe=d;d3d12,d3d12core,dxgi=n,b',WINEDLLPATH=dlls,WINEDLLPATH_PREPEND=dlls,
               DYLD_FALLBACK_LIBRARY_PATH=libs,DYLD_FALLBACK_FRAMEWORK_PATH=str(frameworks)+':/System/Library/Frameworks',WINEMSYNC='1',WINEESYNC='0',
               CX_D3DMETALPATH=str(renderer/'external'),D3DM_MTL4='0',HB_D3D12_REAL=win(shim/'wine/x86_64-windows/apd12.dll'),WINEDEBUG='-all',FH6_D3DMETAL_NO_ANTILAG2='1')
    before=set((build/'sequences').glob('*/sequence.json'));(build/'sequence-request').write_text('isolated eight-frame fixture\n')
    for exe,args in [('test-capture.exe',[]),('test-provider.exe',[win(build/'framegeneration-sequence-inner.dll'),win(build/'framegeneration-sequence.dll')])]:
        log=build/(exe+'.log')
        with log.open('w') as f:result=subprocess.run([str(layer/'engine/bin/wine'),str(build/exe),*args],env=env,cwd=build,stdout=f,stderr=subprocess.STDOUT,timeout=90)
        text=log.read_text(errors='replace')
        for line in text.splitlines():
            if line.startswith(('PASS','FAIL','GPU contrast')):print(line)
        if result.returncode or 'PASS actual D3DMetal device apd12.dll' not in text:raise RuntimeError('GPU probe failed: '+exe)
    captures=set((build/'sequences').glob('*/sequence.json'))-before
    if len(captures)!=1:raise RuntimeError('Expected exactly one new sequence')
    captured=next(iter(captures)).parent;checked=verify_sequence(captured,build)
    receipt={'passed':True,'installed_in_game':False,'renderer':'D3DMetal apd12.dll','sequence_frames':8,'provider_frames_compared':12,'serialized_pixels_checked':checked,
             'manifest_sha256':sha(build/'build.json'),'private_inputs':private_hashes,'artifacts':manifest['artifacts'],'quality_verified':False}
    (build/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('PASS sequence serialisation pixels',checked,'capture',captured)
if __name__=='__main__':main()
