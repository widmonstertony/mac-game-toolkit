#!/usr/bin/env python3
"""Read-only native Steam entry check from an isolated, already prepared overlay."""
import argparse,hashlib,json,os,secrets,shutil,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('overlay','engine','prefix','probe','game','steam-client'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--shim',action='store_true')
p.add_argument('--client',type=Path)
p.add_argument('--debug',action='store_true')
p.add_argument('--stage-isolated-prefix',action='store_true',help='Copy bridge files only into the isolated probe-prefix')
a=p.parse_args()
if a.prefix.resolve().name!='probe-prefix' or not (a.prefix/'system.reg').is_file():raise SystemExit('Existing isolated probe-prefix only')
out=a.overlay.resolve();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
client_dirs=(a.steam_client,a.steam_client/'Steam.AppBundle/Steam/Contents/MacOS',a.steam_client/'Contents/MacOS')
native_client=next((d.resolve() for d in client_dirs if (d/'steamclient.dylib').is_file()),None)
if native_client is None:raise SystemExit('Native steamclient.dylib not found in the supplied Steam installation')
build=json.loads((out/'steam-entry-build.json').read_text())
for name,digest in build['outputs'].items():
    if sha(out/name)!=digest:raise SystemExit('Steam overlay hash mismatch: '+name)
if a.probe.resolve()!=out/'steam-entry-probe.exe':raise SystemExit('Use the verified overlay probe')
if a.stage_isolated_prefix:
    target=a.prefix/'drive_c/Program Files (x86)/Steam'
    if target.is_symlink() or not target.resolve().is_relative_to(a.prefix.resolve()):raise SystemExit('Steam directory escapes isolated prefix')
    target.mkdir(parents=True,exist_ok=True)
    for src in (out/'steam').iterdir():
        dst=target/src.name
        if not src.is_file() or dst.is_symlink():raise SystemExit('Unexpected Steam bridge file')
        shutil.copy2(src,dst)
running=subprocess.run(['/usr/bin/pgrep','-x','steam_osx'],stdout=subprocess.DEVNULL).returncode==0
if not running:
    (out/'entry-validation.json').write_text(json.dumps({'passed':False,'blockedReason':'native-steam-not-running','buildReceiptSha256':sha(out/'steam-entry-build.json'),'gameStarted':False,'installed':False,'gameplayVerified':False},indent=2)+'\n')
    raise SystemExit('Native Steam is not running; no SteamAPI/login retry attempted')
env={k:v for k,v in os.environ.items() if not k.startswith(('DYLD_','WINE','PROTON_','LINUXHOST_'))}
env.update(WINEPREFIX=str(a.prefix),WINEDEBUG='-all',WINEDLLPATH=str(out/'steam')+':'+str(out/'overlay'),WINEDLLOVERRIDES='winemenubuilder.exe=d;steamclient=n;steamclient64=n;lsteamclient=b',SteamAppId='2698940',SteamGameId='2698940',SteamPath='Z:'+str(out/'steam').replace('/','\\'),STEAM_COMPAT_CLIENT_INSTALL_PATH=str(native_client))
log=out/'entry.log'
nonce=secrets.token_hex(16)
result=out/('entry-result-'+nonce+'.txt')
env.update(MOTORFEST_PROBE_RESULT='Z:'+str(result).replace('/','\\'),MOTORFEST_PROBE_NONCE=nonce)
if a.debug:env['WINEDEBUG']='-all,+loaddll,+steamclient,+steam,+module'
command=[str(a.probe),'Z:'+str(a.game/'steam_api64.dll').replace('/','\\')]
if a.client:command.append('Z:'+str(a.client.resolve()).replace('/','\\'))
if a.shim:command.insert(0,str(out/'steam/steam.exe'))
with log.open('w') as f:
    try:rc=subprocess.run([str(a.engine/'MacOS/wine'),*command],env=env,cwd=out/'steam',stdout=f,stderr=subprocess.STDOUT,timeout=45).returncode
    except subprocess.TimeoutExpired:rc=124
text=log.read_text(errors='replace')
# The Steam shim does not necessarily inherit its child's stdout. Its own zero
# exit status is not evidence that SteamAPI initialized in the child.
deadline=time.monotonic()+10
while rc==0 and not result.exists() and time.monotonic()<deadline:time.sleep(.1)
child_completed=result.is_file() and not result.is_symlink() and result.read_text()==f'STEAM_API_INIT_AND_SHUTDOWN_OK {nonce}\n'
passed=rc==0 and child_completed
print('exitCode',rc,'passed',passed)
print('\n'.join(line for line in text.splitlines() if any(w in line for w in ['STEAM_API_','err:module','failed','not running']))[-3000:])
receipt={'exitCode':rc,'passed':passed,'childInitAndShutdownVerified':child_completed,'usedNativeSteamShim':a.shim,'nativeClientSha256':sha(native_client/'steamclient.dylib'),'buildReceiptSha256':sha(out/'steam-entry-build.json'),'probeSha256':sha(a.probe),'apiSha256':sha(a.game/'steam_api64.dll'),'logSha256':sha(log),'gameStarted':False,'installed':False,'gameplayVerified':False}
(out/'entry-validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(0 if passed else 1)
