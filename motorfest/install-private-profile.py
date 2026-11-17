#!/usr/bin/env python3
"""Install/rollback the Motorfest-only candidate after component validation.

No game launch, account reset, graphics-setting edit or global engine replacement.
The existing prefix is cloned before changes. Activation is a user trial, not a
claim that protected gameplay has passed.
"""
import argparse, fcntl, hashlib, json, os, re, shlex, shutil, subprocess
from pathlib import Path

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def clone(src,dst):
    if dst.exists():raise RuntimeError('Backup/destination already exists: '+str(dst))
    subprocess.run(['/bin/cp','-cR',str(src),str(dst)],check=True)
def write_atomic(p,data,mode=None):
    tmp=p.with_name(p.name+'.motorfest-new')
    with tmp.open('xb') as f:f.write(data);f.flush();os.fsync(f.fileno())
    tmp.chmod(mode if mode is not None else p.stat().st_mode&0o777)
    tmp.replace(p)
def restore_owned_runner(runner,original,installed):
    """Never erase another installer/user's subsequent runner edit."""
    current=runner.read_bytes()
    if current==original:return
    if installed is not None and current==installed:
        write_atomic(runner,original)
        return
    raise RuntimeError('Runner changed externally; original preserved in backup, refusing to overwrite')
def stop_prefix(server,prefix):
    if not server.is_file():raise RuntimeError('Scoped Wine server missing')
    env={k:v for k,v in os.environ.items() if not k.startswith(('WINE','DYLD_'))}
    env['WINEPREFIX']=str(prefix)
    subprocess.run([str(server),'-k'],env=env,check=True,timeout=15)
    subprocess.run([str(server),'-w'],env=env,check=True,timeout=15)
def builtin_browser_registry(text):
    for exe in ('upc.exe','UplayWebCore.exe','UbisoftConnect.exe'):
        key=r'Software\\Wine\\AppDefaults\\'+exe+r'\\DllOverrides'
        pattern=r'(?m)^\['+re.escape(key)+r'\][^\n]*\n(?P<body>(?:(?!\n\[).)*)(?=\n\[|\Z)'
        match=re.search(pattern,text,re.S);section=match.group(0) if match else '['+key+']\n'
        for dll in ('d3d11','dxgi'):
            line='"'+dll+'"="builtin"'
            if re.search(r'(?m)^"'+dll+'"=',section):section=re.sub(r'(?m)^"'+dll+r'"=.*$',line,section)
            else:section=section.rstrip()+'\n'+line+'\n'
        text=text[:match.start()]+section+text[match.end():] if match else text+'\n'+section
    return text

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared',type=Path,required=True)
    p.add_argument('--lifecycle-result',type=Path)
    p.add_argument('--apply',action='store_true');p.add_argument('--rollback',action='store_true')
    a=p.parse_args();source=a.prepared.resolve();profile=json.loads((source/'profile.json').read_text())
    data=Path(profile['data']);prefix=data/'pfx';runner=Path(profile['sharedRunner'])
    if data.name!='2698940' or data.parent.name!='compatdata' or prefix.is_symlink():raise SystemExit('Motorfest real prefix only')
    base=data/'.motorfest-private-20261117'
    if a.rollback:
        if not a.apply:print('Would restore original runner and backup prefix, preserving post-test prefix first.');return
        record=json.loads((base/'installed.json').read_text())
        if sha(runner)!=record['installedRunnerSha256']:raise SystemExit('Runner changed since install; refusing to overwrite')
        stop_prefix(base/'MetalSharp-Wine.app/Contents/MacOS/wineserver',prefix)
        with (data/'.notproton-prefix.lock').open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            if (base/'post-test-pfx').exists():raise SystemExit('Rollback preservation already exists')
            # Caller must stop this prefix's Wine processes before rollback.
            prefix.rename(base/'post-test-pfx')
            try:clone(base/'backup-pfx',prefix)
            except BaseException:(base/'post-test-pfx').rename(prefix);raise
            write_atomic(runner,(base/'original-shared-runner').read_bytes())
            record['rolledBack']=True;record['postTestDataPreserved']=True
            write_atomic(base/'installed.json',(json.dumps(record,indent=2)+'\n').encode(),0o600)
        print('Rolled back; post-test data preserved at',base/'post-test-pfx');return
    plan=json.loads((source/'install-plan.json').read_text())
    if (sha(source/'profile.json')!=plan['profileSha256'] or sha(source/'launch.py')!=plan['launcherSha256']
        or sha(source/'runner-hook.sh')!=plan['hookSha256']):
        raise SystemExit('Prepared profile changed')
    if not a.lifecycle_result:raise SystemExit('Lifecycle result required')
    lifecycle=json.loads(a.lifecycle_result.read_text())
    if lifecycle.get('launcherSha256')!=sha(source/'launch.py'):raise SystemExit('Different launcher tested')
    if lifecycle.get('exitCode')!=0 or not lifecycle.get('childDoneBeforeReturn') or lifecycle.get('elapsedSeconds',0)<4:
        raise SystemExit('Steam shim child lifetime not verified')
    if sha(runner)!=profile['sharedRunnerSha256']:raise SystemExit('Original runner changed; reprepare')
    candidate=Path(profile['candidate']);manifest=json.loads((candidate/'candidate.json').read_text())
    if sha(candidate/'candidate.json')!=profile['candidateSha256']:raise SystemExit('Candidate manifest changed')
    for name,digest in manifest['files'].items():
        if sha(candidate/name)!=digest:raise SystemExit('Candidate changed: '+name)
    engine=Path(profile['engine'])
    if sha(engine/'MacOS/wine')!=manifest['loaderSha256']:raise SystemExit('Engine changed')
    browser=prefix/'drive_c/Program Files (x86)/Ubisoft/Ubisoft Game Launcher'
    for target in (browser,prefix/'drive_c/Program Files (x86)/Steam',prefix/'drive_c/windows/system32/beclient_x64.dll'):
        if target.is_symlink() or not target.resolve().is_relative_to(prefix.resolve()):raise SystemExit('Target escapes Motorfest prefix')
    marker=json.loads((browser/'.mac-game-toolkit-browser.json').read_text())
    for name in ('d3d11.dll','dxgi.dll'):
        if sha(browser/name)!=marker['files'][name]['installed']:raise SystemExit('Unrecognized browser DLL: '+name)
    if base.exists():raise SystemExit('Private profile already exists; never overwrite backups')
    print('Exact scope: Motorfest 2698940; same prefix, account data and game files; shared runner has AppID+path guard.')
    if not a.apply:return
    stop_prefix(Path.home()/'Library/Application Support/Highball/engines/x64-crossover26.3-r28/engine/bin/wineserver',prefix)
    with (data/'.notproton-prefix.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        base.mkdir(mode=0o700)
        clone(prefix,base/'backup-pfx')
        shutil.copy2(runner,base/'original-shared-runner')
        clone(candidate,base/'candidate')
        clone(engine.parent,base/'MetalSharp-Wine.app')
        shutil.copy2(source/'launch.py',base/'launch.py')
        profile.update(enabled=True,candidate=str(base/'candidate'),engine=str(base/'MetalSharp-Wine.app/Contents'))
        (base/'profile.json').write_text(json.dumps(profile,indent=2)+'\n');(base/'profile.json').chmod(0o600)
        archive=base/'original-browser-dlls';archive.mkdir()
        patched=None
        try:
            for name in ('d3d11.dll','dxgi.dll'):(browser/name).rename(archive/name)
            registry=prefix/'user.reg';write_atomic(registry,builtin_browser_registry(registry.read_text()).encode())
            target=prefix/'drive_c/Program Files (x86)/Steam';target.mkdir(parents=True,exist_ok=True)
            for f in (base/'candidate/steam-entry/steam').iterdir():
                if f.is_file():shutil.copy2(f,target/f.name)
            shutil.copy2(base/'candidate/overlay/x86_64-windows/beclient_x64.dll',prefix/'drive_c/windows/system32/beclient_x64.dll')
            hook=(source/'runner-hook.sh').read_text().replace(shlex.quote(str(source/'launch.py')),shlex.quote(str(base/'launch.py')))
            original=runner.read_bytes();first,rest=original.split(b'\n',1)
            patched=first+b'\n'+hook.encode()+rest
            if sha(runner)!=profile['sharedRunnerSha256']:raise RuntimeError('Runner changed during installation')
            write_atomic(runner,patched)
        except BaseException:
            # No game was launched; preserve the failed migration before restore.
            prefix.rename(base/'failed-install-pfx');clone(base/'backup-pfx',prefix)
            restore_owned_runner(runner,(base/'original-shared-runner').read_bytes(),patched)
            raise
        receipt=dict(appId=2698940,enabled=True,gameplayVerified=False,installedRunnerSha256=sha(runner),
          originalRunnerSha256=sha(base/'original-shared-runner'),candidateSha256=sha(base/'candidate/candidate.json'),
          prefixBackup=str(base/'backup-pfx'),privateLauncherSha256=sha(base/'launch.py'),
          protectedStartupPending=True,settingsChanged=False,otherGameRoutesChanged=False)
        (base/'installed.json').write_text(json.dumps(receipt,indent=2)+'\n');(base/'installed.json').chmod(0o600)
        print('Installed Motorfest candidate for next native Steam launch; gameplay remains unverified.')
if __name__=='__main__':main()
