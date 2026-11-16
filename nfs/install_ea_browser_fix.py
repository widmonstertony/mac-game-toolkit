#!/usr/bin/env python3
"""Give only EA's browser the working Heat DXMT renderer, leaving DX12 games alone."""
import argparse
from pathlib import Path
import re
import subprocess
import hashlib
import json
import shutil

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--apply',action='store_true')
    args=p.parse_args()
    prefix=Path.home()/'Library/Application Support/Steam/steamapps/compatdata/1222680/pfx'
    reg=prefix/'user.reg'
    original=reg.read_text(); changed=original
    app_root=prefix/'drive_c/Program Files/Electronic Arts/EA Desktop'
    apps=[f.parent for f in app_root.glob('*/EA Desktop/EADesktop.exe')]
    if len(apps)!=1:raise RuntimeError('Expected one active versioned EA installation')
    app=apps[0]
    dxmt=Path.home()/'Library/Application Support/Highball/engines/x64-crossover26.3-r28/renderers/dxmt/wine/x86_64-windows'
    payload={}
    original_hashes={}
    for name in ['d3d11.dll','dxgi.dll']:
        data=bytearray((dxmt/name).read_bytes())
        original_hashes[name]=hashlib.sha256(data).hexdigest()
        if data[64:81]!=b'Wine builtin DLL\0':raise RuntimeError('DXMT loader marker changed')
        # The bundled DXMT is tagged builtin. Wine redirects that marker back
        # to WINEDLLPATH, which selects D3DMetal even with a local native override.
        # Remove only the DOS-stub marker in these private EA copies. PE sections
        # and all renderer code remain byte-for-byte identical to the source.
        data[64:81]=bytes(17)
        payload[name]=data
    files={name:hashlib.sha256(data).hexdigest() for name,data in payload.items()}
    for name,digest in files.items():
        dst=app/name
        if dst.exists() and hashlib.sha256(dst.read_bytes()).hexdigest() not in (digest,original_hashes[name]):
            raise RuntimeError('Unrecognized existing EA-local renderer: '+str(dst))
    for exe in ['EADesktop.exe','EACefSubProcess.exe']:
        key=r'Software\\Wine\\AppDefaults\\'+exe+r'\\DllOverrides'
        pattern=r'(?m)^\['+re.escape(key)+r'\][^\n]*\n(?P<body>(?:(?!\n\[).)*)(?=\n\[|\Z)'
        match=re.search(pattern,changed,re.S)
        if match:
            body=match.group(0)
            # Remove our unsuccessful Vulkan-only candidate: changing it alone
            # did not fix the CEF surface or the blank login view.
            body=re.sub(r'(?m)^"vulkan-1"=""\n?', '',body)
            for dll in ['d3d11','dxgi']:
                line='"'+dll+'"="native,builtin"'
                if re.search(r'(?m)^"'+dll+r'"=',body):
                    body=re.sub(r'(?m)^"'+dll+r'"=.*$',line,body)
                else:body=body.rstrip()+'\n'+line+'\n'
            changed=changed[:match.start()]+body+changed[match.end():]
        else:changed+='\n['+key+']\n"d3d11"="native,builtin"\n"dxgi"="native,builtin"\n'
    print('EA browser: local DXMT, shared Heat container. Unbound game: existing D3DMetal DX12.')
    if not args.apply:return
    ps=subprocess.check_output(['/bin/ps','-axo','comm='],text=True).lower()
    if any(x in ps for x in ['needforspeed','eadesktop.exe','eacefsubprocess.exe','eabackgroundservice.exe']):
        raise RuntimeError('Close shared EA/NFS processes before updating their registry')
    backup=reg.with_name('user.reg.before-ea-cef-vulkan-20261115')
    if not backup.exists():backup.write_bytes(reg.read_bytes())
    for name,data in payload.items():(app/name).write_bytes(data)
    reg.write_text(changed)
    (app/'.nfs-ea-renderer.json').write_text(json.dumps({'source':str(dxmt),'sourceHashes':original_hashes,'files':files,'loaderMarkerRemoved':True,'gameRendererUnchanged':True},indent=2)+'\n')

if __name__=='__main__':main()
