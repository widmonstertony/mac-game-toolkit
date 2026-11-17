#!/usr/bin/env python3
"""Install the validated DXGI change only in Motorfest's private prefix."""
import argparse,datetime,hashlib,json,os,shutil,subprocess
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--dll',type=Path,required=True);ap.add_argument('--proof',type=Path,required=True)
a=ap.parse_args()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
data=Path.home()/'Library/Application Support/Steam/steamapps/compatdata/2698940'
private=data/'.motorfest-private-20261117';profile_path=private/'profile.json'
p=json.loads(profile_path.read_text());e=json.loads(a.proof.read_text());engine=Path(p['engine'])
assert Path(p['data']).resolve()==data.resolve() and p['appId']==2698940
assert p['enabled'] and e['gpuReadback'] and e['factory7IdentityEventsLifetime']
assert e['dllSha256']==sha(a.dll)
engine_dxgi=engine/'lib/mullion/aarch64-windows/dxgi.dll'
assert e['engineDxgiSha256']==sha(engine_dxgi)
assert e['sourceCommit']=='39d1e4a40121ddfe4f9fc3793bfc336f4491d5b5'
# No global kill: Ubisoft background services from this game's private prefix only.
env=dict(os.environ,WINEPREFIX=str(data/'pfx'))
for option in ('-k','-w'):
 result=subprocess.run([str(engine/'MacOS/wineserver'),option],env=env,timeout=20)
 if result.returncode not in (0,1):raise RuntimeError('Motorfest wineserver stop failed')
backup=private/('factory7-'+datetime.datetime.now().strftime('%Y%m%dT%H%M%S'));backup.mkdir()
target=data/'pfx/drive_c/windows/system32/dxgi.dll'
assert not target.is_symlink(), 'Refuse to overwrite a library symlink'
for path in (target,profile_path,private/'launch.py',private/'installed.json'):
 if path.exists():shutil.copy2(path,backup/path.name)
shutil.copy2(a.proof,backup/'proof.json')
tmp=target.with_name('dxgi.factory7-new.dll');shutil.copy2(a.dll,tmp);os.replace(tmp,target)
runner=Path(__file__).resolve().parents[1]/'private-runner.py'
shutil.copy2(runner,private/'launch.py')
p['firstFaultDiagnostics']=bool(e.get('firstFaultContinuesToOriginalHandler'));p['factory7DxgiNative']=True;p['factory7DxgiSha256']=sha(target);p['gameplayVerified']=False
profile_path.write_text(json.dumps(p,indent=2)+'\n')
receipt={'backup':str(backup),'dxgiSha256':sha(target),'engineDxgiSha256':sha(engine_dxgi),'runnerSha256':sha(private/'launch.py'),'gameplayVerified':False,'battlEyeHandshakeVerified':False}
(backup/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
