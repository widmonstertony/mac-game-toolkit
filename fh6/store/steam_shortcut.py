#!/usr/bin/env python3
"""Add a real non-Steam Store launcher; never fake a Steam installation manifest."""
import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import struct
import subprocess
import tempfile
import zlib


def cstring(data, pos):
    end=data.index(b'\0',pos)
    return data[pos:end].decode('utf-8'),end+1


def entry(data, pos):
    start=pos;kind=data[pos];name,pos=cstring(data,pos+1)
    if kind==0:
        value=[]
        while data[pos]!=8:
            child=entry(data,pos);value.append(child);pos=child['end']
        pos+=1
    elif kind==1:value,pos=cstring(data,pos)
    elif kind==2:value=struct.unpack_from('<I',data,pos)[0];pos+=4
    else:raise ValueError('Unsupported VDF type; file left unchanged: '+str(kind))
    return dict(start=start,end=pos,kind=kind,name=name,value=value)


def string(key,value):return b'\x01'+key.encode()+b'\0'+value.encode()+b'\0'
def integer(key,value):return b'\x02'+key.encode()+b'\0'+struct.pack('<I',value)


def append_shortcut(data, app):
    if not data:data=b'\0shortcuts\0\x08\x08'
    root=entry(data,0)
    if root['kind']!=0 or root['name']!='shortcuts' or data[root['end']:]!=b'\x08':
        raise ValueError('Unexpected shortcut root; no overwrite')
    executable='"'+str(app)+'"';title='Forza Horizon 6 (Xbox)'
    appid=zlib.crc32((executable+title).encode())|0x80000000
    children=root['value']
    for child in children:
        values={c['name'].lower():c['value'] for c in child['value']}
        if values.get('exe')==executable:
            if values.get('appname')!=title:raise ValueError('Existing launcher has a different name; review it')
            return data,values['appid'],False
        if values.get('appid')==appid:raise ValueError('Shortcut ID collision; no overwrite')
    index=max([int(c['name']) for c in children],default=-1)+1
    payload=(integer('appid',appid)+string('AppName',title)+string('Exe',executable)+
             string('StartDir','"'+str(app.parent)+'"')+string('icon',str(app/'Contents/Resources/game.icns'))+
             string('ShortcutPath','')+string('LaunchOptions','')+
             b''.join(integer(k,v) for k,v in {'IsHidden':0,'AllowDesktopConfig':1,'AllowOverlay':0,'OpenVR':0,'Devkit':0,'DevkitOverrideAppID':0,'LastPlayTime':0}.items())+
             string('DevkitGameID','')+string('FlatpakAppID','')+b'\0tags\0'+string('0','Xbox / Microsoft Store')+b'\x08')
    new=b'\0'+str(index).encode()+b'\0'+payload+b'\x08'
    split=root['end']-1
    updated=data[:split]+new+data[split:]
    parsed=entry(updated,0)
    assert len(parsed['value'])==len(children)+1
    for old,fresh in zip(children,parsed['value']):
        assert data[old['start']:old['end']]==updated[fresh['start']:fresh['end']]
    return updated,appid,True


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--apply',action='store_true');a=p.parse_args()
    home=Path.home();app=home/'Applications/Forza Horizon 6 Xbox.app'
    with (app/'Contents/Info.plist').open('rb') as f:info=plistlib.load(f)
    if info['CFBundleIdentifier']!='local.macgametoolkit.fh6.store':raise RuntimeError('Unexpected launcher')
    configs=[q/'config' for q in (home/'Library/Application Support/Steam/userdata').iterdir()
             if q.name.isdecimal() and q.name!='0' and (q/'config').is_dir()]
    if len(configs)!=1:raise RuntimeError('Select the intended Steam account configuration explicitly')
    file=configs[0]/'shortcuts.vdf';before=file.read_bytes() if file.exists() else b''
    after,appid,changed=append_shortcut(before,app)
    print('Store non-Steam launcher ready; existing shortcut bytes preserved; change=',changed)
    print('Launch URI: steam://rungameid/'+str((appid<<32)|0x02000000))
    if not a.apply or not changed:return
    comm=subprocess.check_output(['/bin/ps','-axo','comm='],text=True)
    if any(line.strip().endswith('/steam_osx') for line in comm.splitlines()):
        raise RuntimeError('Quit native Steam normally before changing shortcuts.vdf')
    if file.exists() and file.read_bytes()!=before:raise RuntimeError('Steam shortcut file changed during preparation')
    receipt=home/'Library/Application Support/MACxus/external-highball'
    backup=receipt/'steam-shortcuts-before-store.vdf'
    if not backup.exists():backup.write_bytes(before)
    fd,tmp=tempfile.mkstemp(prefix='.fh6-store-shortcuts-',dir=file.parent)
    try:
        with os.fdopen(fd,'wb') as f:f.write(after);f.flush();os.fsync(f.fileno())
        os.replace(tmp,file)
    finally:
        if os.path.exists(tmp):os.unlink(tmp)
    print('Registered Forza Horizon 6 (Xbox) in native Steam. No Steam manifest or cloud configuration was fabricated.')


if __name__=='__main__':main()
