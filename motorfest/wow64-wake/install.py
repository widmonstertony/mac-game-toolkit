#!/usr/bin/env python3
"""Select an isolated engine clone for the next Motorfest launch; no process kill."""
import argparse,datetime,hashlib,json,os,shutil,subprocess
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--dll',type=Path,required=True);ap.add_argument('--apply',action='store_true');ap.add_argument('--resume',action='store_true');a=ap.parse_args()
root=Path(__file__).resolve().parent;proof=json.loads((root/'validation-2026-11-17.json').read_text())
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert sha(a.dll)==proof['candidateSha256']
assert proof['exportsNamesOrdinalsIdentical'] and proof['exportCount']==1509
assert any(x['rc'] for x in proof['comparison'] if x['mode']=='baseline')
assert len([x for x in proof['comparison'] if x['mode']=='candidate' and x['rc']==0])==8
assert all(x['rc']==0 for x in proof['contract'])
data=Path.home()/'Library/Application Support/Steam/steamapps/compatdata/2698940'
private=data/'.motorfest-private-20261117';profile=private/'profile.json';before=profile.read_bytes();p=json.loads(before)
assert p['appId']==2698940 and Path(p['data']).resolve()==data.resolve() and p['enabled']
old=Path(p['engine']).resolve();assert old.is_relative_to(private) and old.name=='Contents'
assert sha(old/'lib/wine/i386-windows/ntdll.dll')==proof['originalSha256']
new_app=private/('sync-engine-'+proof['candidateSha256'][:12])/'MetalSharp-Wine.app'
plan={'previousEngine':str(old),'nextEngine':str(new_app/'Contents'),'candidateSha256':proof['candidateSha256'],'takesEffect':'next launch only','currentGameRestarted':False}
if not a.apply:print(json.dumps(plan,indent=2));raise SystemExit(0)
if new_app.exists():
 assert a.resume and sha(new_app/'Contents/lib/wine/i386-windows/ntdll.dll')==proof['candidateSha256'], 'Unrecognized existing stage'
else:
 new_app.parent.mkdir();subprocess.run(['cp','-cR',str(old.parent),str(new_app)],check=True)
target=new_app/'Contents/lib/wine/i386-windows/ntdll.dll';temp=target.with_suffix('.new');shutil.copy2(a.dll,temp);os.chmod(temp,(old/'lib/wine/i386-windows/ntdll.dll').stat().st_mode&0o777);os.replace(temp,target)
# Prove all regular files, modes and symlinks other than the scoped PE DLL retain identity.
changed=[]
for f in old.parent.rglob('*'):
 rel=f.relative_to(old.parent);g=new_app/rel
 if f.is_symlink():assert g.is_symlink() and os.readlink(f)==os.readlink(g)
 elif f.is_file():
  assert g.is_file() and (f.stat().st_mode&0o777)==(g.stat().st_mode&0o777)
  if sha(f)!=sha(g):changed.append(str(rel))
assert changed==['Contents/lib/wine/i386-windows/ntdll.dll'], changed
assert profile.read_bytes()==before, 'Profile changed during staging'
backup=private/('wake-backup-'+datetime.datetime.now().strftime('%Y%m%dT%H%M%S'));backup.mkdir();(backup/'profile.json').write_bytes(before);shutil.copy2(root/'validation-2026-11-17.json',backup/'proof.json')
p.update(wow64WakeRecoveryPendingRestart=True,engine=str(new_app/'Contents'),wow64WakeRecoverySha256=proof['candidateSha256'],wow64WakeRecoveryPreviousEngine=str(old))
shutil.copy2(private/'launch.py',backup/'launch.py');shutil.copy2(root.parent/'private-runner.py',private/'launch.py')
next_profile=profile.with_suffix('.new');next_profile.write_text(json.dumps(p,indent=2)+'\n');os.replace(next_profile,profile)
plan.update(backup=str(backup),changedFiles=changed,originalEngineUnchanged=sha(old/'lib/wine/i386-windows/ntdll.dll')==proof['originalSha256'])
(backup/'receipt.json').write_text(json.dumps(plan,indent=2)+'\n');print(json.dumps(plan,indent=2))
