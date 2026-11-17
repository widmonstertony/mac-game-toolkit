#!/usr/bin/env python3
"""Install a validated private ICD; never replace the signed engine or shared Wine."""
import argparse, datetime, hashlib, json, os, shutil, subprocess
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--library',type=Path,required=True);a=ap.parse_args()
root=Path(__file__).resolve().parent
proof=json.loads((root/'validation-2026-11-17.json').read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
data=Path.home()/'Library/Application Support/Steam/steamapps/compatdata/2698940'
private=data/'.motorfest-private-20261117';profile_path=private/'profile.json'
p=json.loads(profile_path.read_text());engine=Path(p['engine'])
assert p['appId']==2698940 and Path(p['data']).resolve()==data.resolve() and p['enabled']
assert proof['patchedGpuReadback64'] and proof['robustnessNullDescriptorRegression']
assert proof['highSet16GpuReadback'] and proof['highestSet18WithPushConstantsGpuReadback']
assert proof['workgroup4840GpuReadback'] and proof['actualSsaoShaderPipelineCreated']
assert sha(a.library)==proof['candidateSha256']
assert sha(engine/'lib/libMoltenVK.dylib')==proof['originalSha256']
backup=private/('shader-address-space-'+datetime.datetime.now().strftime('%Y%m%dT%H%M%S'));backup.mkdir()
for item in ('profile.json','launch.py','installed.json'):
 if (private/item).exists():shutil.copy2(private/item,backup/item)
shutil.copy2(root/'validation-2026-11-17.json',backup/'proof.json')
stage=private/'graphics'/('address-space-'+proof['candidateSha256'][:12]);stage.mkdir(parents=True,exist_ok=True)
lib=stage/'libMoltenVK.dylib';shutil.copy2(a.library,lib)
assert sha(lib)==proof['candidateSha256']
icd=stage/'MoltenVK_icd.json';icd.write_text(json.dumps({'file_format_version':'1.0.0','ICD':{'library_path':str(lib),'api_version':'1.4.0','is_portability_driver':True}},indent=2)+'\n')
# Stop only this private prefix after all new files are staged.
env=dict(os.environ,WINEPREFIX=str(data/'pfx'))
for option in ('-k','-w'):
 result=subprocess.run([str(engine/'MacOS/wineserver'),option],env=env,timeout=25)
 if result.returncode not in (0,1):raise RuntimeError('Scoped Wine stop failed')
runner=root.parent/'private-runner.py'
shutil.copy2(runner,private/'launch.py')
p.update(moltenvkAddressSpaceIcd=str(icd),moltenvkAddressSpaceSha256=sha(lib),gameplayVerified=False)
# Full shader dumping was only needed to identify the resource-set collision.
p.pop('shaderDumpDirectory',None)
new=profile_path.with_suffix('.new');new.write_text(json.dumps(p,indent=2)+'\n');os.replace(new,profile_path)
receipt={'backup':str(backup),'library':str(lib),'librarySha256':sha(lib),'engineUnchanged':sha(engine/'lib/libMoltenVK.dylib')==proof['originalSha256'],'privateLauncherSha256':sha(private/'launch.py'),'actualGameRenderVerified':False,'battlEyeHandshakeVerified':False}
(backup/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
