#!/usr/bin/env python3
"""Configure an external Store/Highball runner; never modify the MACxus app."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

HERE = Path(__file__).resolve().parent
UPSTREAM_ASAR = '927cf9e8d91b293104ce621363f2141544cbd87b82ec66b201c519c41b36152d'


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def configure(app, engine, data):
    archive = app/'Contents/Resources/app.asar'
    if digest(archive) != UPSTREAM_ASAR:
        raise RuntimeError('Expected the verified unmodified MACxus v0.3.11-mac.12 archive')
    renderer, libraries, shim = engine/'renderers/d3dmetal',engine/'frameworks',engine/'renderers/d3dmetal-tsshim'
    paths = [engine/'engine/bin/wine',engine/'engine/bin/wineserver',engine/'engine/lib/wine/x86_64-windows/kernelbase.dll',
             renderer/'wine/x86_64-windows/d3d12.dll',renderer/'external/libd3dshared.dylib',libraries/'libgnutls.30.dylib',
             shim/'wine/x86_64-windows/d3d12.dll',shim/'wine/x86_64-windows/apd12.dll',shim/'wine/x86_64-unix/apd12.so']
    config = {'schema':1,'kind':'existing-highball','engineRoot':str(engine),'rendererRoot':str(renderer),
              'libraryRoot':str(libraries),'shimRoot':str(shim),'sha256':{str(p):digest(p) for p in paths}}
    root = data/'external-highball'
    root.mkdir(parents=True,exist_ok=True)
    raw=archive.read_bytes()
    marker,length,payload,size=struct.unpack_from('<IIII',raw)
    if marker != 4 or payload+4 != length:
        raise RuntimeError('Unsupported ASAR header')
    tree=json.loads(raw[16:16+size])
    def entries(v,prefix=''):
        for name,item in v.get('files',{}).items():
            current=prefix+name
            if 'files' in item:
                yield from entries(item,current+'/')
            else:
                yield current,item
    copied={}
    for name,item in entries(tree):
        if not (name.startswith(('dist-electron/main/','dist-electron/shared/','assets/')) or name == 'package.json'):
            continue
        if 'offset' not in item:
            continue
        target=root/'upstream'/name
        if not target.resolve().is_relative_to((root/'upstream').resolve()):
            raise RuntimeError('Unsafe archive path')
        start=8+length+int(item['offset'])
        value=raw[start:start+item['size']]
        if len(value) != item['size']:
            raise RuntimeError('Truncated entry')
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(value)
        copied[name]=hashlib.sha256(value).hexdigest()
    for name in ['local-highball.js','runner.mjs']:
        shutil.copy2(HERE/name,root/name)
    (root/'package.json').write_text('{"private":true,"type":"module"}\n')
    (data/'local-highball.json').write_text(json.dumps(config,indent=2)+'\n')
    node=shutil.which('node')
    if not node:
        raise RuntimeError('A native Node.js installation is required')
    script=data/'Forza-Store.command'
    # Fixed interpreter and argument forwarding; no game or account data is
    # embedded. The command requires a real installed Store package directory.
    import shlex
    script.write_text('#!/bin/sh\nexec '+shlex.quote(node)+' '+shlex.quote(str(root/'runner.mjs'))+' run "$@"\n')
    script.chmod(0o755)
    receipt={'upstream_tag':'v0.3.11-mac.12','app_asar_sha256':digest(archive),
             'installed_app_unmodified':True,'resigned':False,'upstream_modules_unmodified':copied,
             'game_downloaded':False,'gameplay_verified':False,'steam_environment_changed':False}
    (root/'setup-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return root,node


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app',type=Path,default=Path('/Applications/MACxus.app'))
    p.add_argument('--engine',type=Path,default=Path.home()/'Library/Application Support/Highball/engines/x64-crossover26.3-r28')
    p.add_argument('--data',type=Path,default=Path.home()/'Library/Application Support/MACxus')
    p.add_argument('--bootstrap',action='store_true')
    p.add_argument('--vc-source',type=Path,help='Existing verified x64 Microsoft VC++ system32 directory')
    args=p.parse_args()
    root,node=configure(args.app,args.engine,args.data)
    if args.vc_source:
        import importlib.util
        spec=importlib.util.spec_from_file_location('fh6_rosetta_profile',HERE.parent/'rosetta/profile.py')
        module=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        dest=args.data/'bottles/xbox-highball/drive_c/windows/system32'
        if not (args.data/'bottles/xbox-highball/system.reg').is_file():
            raise RuntimeError('Initialize the new prefix before copying a verified VC++ runtime')
        hashes={}
        for name in ['concrt140.dll','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll']:
            source=args.vc_source/name
            if not module.is_native_x64(source):
                raise RuntimeError('Expected native x64 VC++ runtime: '+name)
            shutil.copy2(source,dest/name)
            hashes[name]=digest(dest/name)
        (root/'vc-runtime-reused.json').write_text(json.dumps(hashes,indent=2)+'\n')
    print('External runner configured; installed MACxus and Steam unchanged.')
    if args.bootstrap:
        subprocess.run([node,str(root/'runner.mjs'),'setup'],check=True)


if __name__=='__main__':
    main()
