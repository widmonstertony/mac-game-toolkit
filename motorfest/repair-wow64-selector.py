#!/usr/bin/env python3
"""Repair the legacy x86 CPU selector only in Motorfest's installed FEX prefix."""
import argparse, datetime, fcntl, hashlib, json, os, re, shutil, subprocess
from pathlib import Path

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def corrected_registry(text):
    key=r'Software\\Microsoft\\Wow64\\x86'
    pattern=r'(?m)^\['+re.escape(key)+r'\][^\n]*\n(?P<body>(?:(?!\n\[).)*)(?=\n\[|\Z)'
    match=re.search(pattern,text,re.S)
    if not match: raise ValueError('Missing x86 CPU selector; refusing to guess')
    section=match.group(0)
    if '@="libwow64fex.dll"' in section: return text
    if section.count('@="wow64cpu.dll"')!=1: raise ValueError('Unknown x86 CPU selector')
    return text[:match.start()]+section.replace('@="wow64cpu.dll"','@="libwow64fex.dll"')+text[match.end():]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--proof',type=Path,required=True)
    p.add_argument('--apply',action='store_true')
    a=p.parse_args()
    data=Path.home()/'Library/Application Support/Steam/steamapps/compatdata/2698940'
    private=data/'.motorfest-private-20261117';prefix=data/'pfx'
    profile=json.loads((private/'profile.json').read_text());engine=private/'MetalSharp-Wine.app/Contents'
    if prefix.is_symlink() or profile['data']!=str(data) or profile['engine']!=str(engine) or not profile['enabled']:
        raise SystemExit('Installed Motorfest private profile required')
    proof=json.loads(a.proof.read_text())
    if not proof['old-selector']['stackOverflow'] or proof['old-selector']['passed'] or proof['fex-selector']['exitCode']!=0 or not proof['fex-selector']['passed'] or proof['fex-selector']['stackOverflow']:
        raise SystemExit('Causal WOW64 comparison required')
    for name in ('MacOS/wine','lib/wine/aarch64-windows/libwow64fex.dll','lib/wine/aarch64-unix/libwow64fex.so'):
        if sha(engine/name)!=proof['engineFiles'][name]: raise SystemExit('Different runtime tested')
    reg=prefix/'system.reg'
    corrected_registry(reg.read_text())
    if not a.apply: print('Would replace only Motorfest x86 CPU selector, preserving all other registry values.');return
    env={k:v for k,v in os.environ.items() if not k.startswith(('WINE','DYLD_'))}
    env['WINEPREFIX']=str(prefix)
    server=engine/'MacOS/wineserver'
    result=subprocess.run([str(server),'-k'],env=env,timeout=15)
    if result.returncode not in (0,1): result.check_returncode()
    subprocess.run([str(server),'-w'],env=env,check=True,timeout=15)
    with (data/'.notproton-prefix.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        original=reg.read_bytes();updated=corrected_registry(original.decode()).encode()
        if original==updated: print('Motorfest x86 selector already correct.');return
        backup=private/('wow64-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
        backup.mkdir(mode=0o700);shutil.copy2(reg,backup/'system.reg')
        shutil.copy2(a.proof,backup/'isolated-comparison.json')
        tmp=reg.with_name('system.reg.motorfest-new')
        with tmp.open('xb') as f:f.write(updated);f.flush();os.fsync(f.fileno())
        tmp.chmod(reg.stat().st_mode&0o777);tmp.replace(reg)
        (backup/'result.json').write_text(json.dumps(dict(oldSha256=hashlib.sha256(original).hexdigest(),newSha256=sha(reg),
          selector='libwow64fex.dll',gameplayVerified=False,otherGameRoutesChanged=False),indent=2)+'\n')
        print('Motorfest-only x86 selector repaired. Receipt:',backup/'result.json')

if __name__=='__main__':main()
