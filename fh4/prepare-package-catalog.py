#!/usr/bin/env python3
"""Prepare real FH4/VCLibs package metadata for an isolated Wine prefix.

No licensing result, MSIXVC marker or game readiness flag is synthesized.
The verified Microsoft dependency is unpacked into its own package directory.
"""
import argparse, hashlib, json, zipfile
from pathlib import Path, PurePosixPath
import xml.etree.ElementTree as ET
from install_vclibs import PACKAGE_SHA256

KEY=r'Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\PackageRepository\Packages'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def identity(raw):
    if len(raw)>1024*1024 or b'<!DOCTYPE' in raw.upper():raise ValueError('Unsupported XML')
    root=ET.fromstring(raw);ns={'p':'http://schemas.microsoft.com/appx/manifest/foundation/windows10'}
    item=root.find('p:Identity',ns)
    if item is None:raise ValueError('Missing identity')
    v=dict(item.attrib)
    if v.get('ProcessorArchitecture')!='x64':raise ValueError('x64 package required')
    publisher=v['Publisher'];bits=int.from_bytes(hashlib.sha256(publisher.encode('utf-16le')).digest()[:8],'big')<<1
    alphabet='0123456789abcdefghjkmnpqrstvwxyz'
    publisher_id=''.join(alphabet[(bits>>(5*i))&31] for i in reversed(range(13)))
    v['publisherId']=publisher_id
    v['fullName']='_'.join((v['Name'],v['Version'],'x64',v.get('ResourceId',''),publisher_id))
    v['familyName']=v['Name']+'_'+publisher_id
    return v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('game','dependency','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();game=a.game.resolve();out=a.out.resolve()
    game_id=identity((game/'AppxManifest.xml').read_bytes())
    if game_id['Name']!='Microsoft.SunriseBaseGame' or not (game/'ForzaHorizon4.exe').is_file():raise SystemExit('Owned extracted FH4 required')
    if sha(a.dependency)!=PACKAGE_SHA256:raise SystemExit('Microsoft FE3 package hash mismatch')
    with zipfile.ZipFile(a.dependency) as z:
        dep_id=identity(z.read('AppxManifest.xml'))
        if dep_id['Name']!='Microsoft.VCLibs.140.00':raise SystemExit('Wrong dependency')
        entries=[]
        for f in z.infolist():
            path=PurePosixPath(f.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in f.filename or ':' in f.filename:raise SystemExit('Unsafe package path')
            if (f.external_attr>>16)&0o170000==0o120000:raise SystemExit('Symlink in package')
            entries.append(f)
        out.mkdir(parents=True,exist_ok=False);dep_root=out/'packages'/dep_id['fullName'];dep_root.mkdir(parents=True)
        for f in entries:
            target=dep_root/f.filename
            if f.is_dir():target.mkdir(parents=True,exist_ok=True)
            else:
                target.parent.mkdir(parents=True,exist_ok=True)
                with target.open('xb') as stream:stream.write(z.read(f))
    records=[];lines=['Windows Registry Editor Version 5.00','']
    for ident,path in ((game_id,game),(dep_id,dep_root)):
        win='Z:'+str(path).replace('/','\\')
        lines += ['[HKEY_LOCAL_MACHINE\\'+KEY+'\\'+ident['fullName']+']',
                  '"Path"="'+win.replace('\\','\\\\').replace('"','\\"')+'"','']
        records.append(dict(identity=ident,path=str(path),manifestSha256=sha(path/'AppxManifest.xml')))
    (out/'catalog.reg').write_text('\n'.join(lines),encoding='utf-16')
    (out/'catalog.json').write_text(json.dumps(dict(packages=records,dependencyArchiveSha256=PACKAGE_SHA256,
      dependencyFiles={str(f.relative_to(dep_root)):sha(f) for f in dep_root.rglob('*') if f.is_file()},
      registryImported=False,gameplayVerified=False,licensingModified=False),indent=2)+'\n')
    print('Prepared actual package directories and catalog:',out)
if __name__=='__main__':main()
