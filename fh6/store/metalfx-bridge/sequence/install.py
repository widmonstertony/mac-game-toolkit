#!/usr/bin/env python3
"""Stage/activate the verified, FH6-only outer recorder, retaining inner bytes."""
import argparse,datetime,hashlib,json,os,shutil,subprocess,tempfile
from pathlib import Path

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def replace(src,dst):
    fd,tmp=tempfile.mkstemp(prefix='.'+dst.name+'.',dir=dst.parent);os.close(fd)
    try:shutil.copy2(src,tmp);os.replace(tmp,dst)
    finally:Path(tmp).unlink(missing_ok=True)
def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--apply',action='store_true');a=p.parse_args();build=a.build.resolve()
    proof=json.loads((build/'verification.json').read_text());manifest=json.loads((build/'build.json').read_text())
    candidate=build/'framegeneration-sequence.dll'
    if not proof.get('passed') or proof.get('sequence_frames')!=8 or proof.get('provider_frames_compared')!=12 or proof.get('serialized_pixels_checked')!=491520 or proof.get('renderer')!='D3DMetal apd12.dll':raise RuntimeError('Complete five-image real-GPU forwarding/serialization proof required')
    if proof['manifest_sha256']!=sha(build/'build.json') or proof['artifacts']!=manifest['artifacts'] or sha(candidate)!=proof['artifacts']['framegeneration-sequence.dll']:raise RuntimeError('Candidate/proof mismatch')
    root=Path.home()/'Library/Application Support/MACxus';runtime=root/'external-highball'
    folder=root/'bottles/xbox-highball/drive_c/highball/fg-native-ui';target=folder/'amd_fidelityfx_framegeneration_dx12.dll';inner=folder/'framegeneration-sequence-inner.dll'
    profile=runtime/'fh6-profile.json';profile_bytes=profile.read_bytes();record=json.loads(profile_bytes)
    old=sha(target);new=sha(candidate)
    if old==new and inner.is_file() and sha(inner)==proof['private_inputs']['framegeneration-sequence-inner.dll']:
        print('Verified recorder already installed; game quality remains unverified.');return
    upgrading=inner.exists()
    inner_digest=proof['private_inputs']['framegeneration-sequence-inner.dll']
    if inner.is_symlink():raise RuntimeError('Refuse a symlinked inner provider')
    if upgrading:
        active=record.get('sequenceCapture',{})
        if active.get('outerSha256')!=old or active.get('innerSha256')!=inner_digest or sha(inner)!=inner_digest:raise RuntimeError('Existing recorder provenance mismatch')
    expected={(inner if upgrading else target):inner_digest,folder/'framegeneration-source.dll':proof['private_inputs']['framegeneration-source.dll'],folder/'framegeneration-original.dll':proof['private_inputs']['framegeneration-original.dll'],folder.parent/'store-plugins/fh6-fg-chain.asi':proof['private_inputs']['fh6-fg-chain.asi']}
    for path,digest in expected.items():
        if path.is_symlink() or sha(path)!=digest or record['verifiedFiles'].get(str(path))!=digest:raise RuntimeError('Installed provider/observer differs from the validated input: '+path.name)
    if upgrading and record['verifiedFiles'].get(str(target))!=old:raise RuntimeError('Active outer provider differs from profile')
    print(json.dumps({'operation':'FH6-only consecutive recorder','upgrade':upgrading,'inner_sha256':inner_digest,'outer_sha256':new,'rendering_changes':False,'restart_required':True,'apply':a.apply},indent=2))
    if not a.apply:return
    processes=subprocess.check_output(['/bin/ps','-axo','comm='],text=True)
    if any(line.strip().lower().endswith('forzahorizon6.exe') for line in processes.splitlines()):raise RuntimeError('Exit FH6 before replacing its DLL')
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    backup=runtime/('sequence-backup-'+stamp);backup.mkdir(exist_ok=False)
    shutil.copy2(target,backup/target.name);shutil.copy2(profile,backup/profile.name)
    record['verifiedFiles'][str(inner)]=inner_digest;record['verifiedFiles'][str(target)]=new
    record['sequenceCapture']={'schema':2,'purpose':'eight consecutive GPU-fenced frames including scene before FG; not a ghosting fix','innerSha256':inner_digest,'outerSha256':new,'gameplayVerified':False}
    with tempfile.TemporaryDirectory(dir=runtime) as tmp:
        staged=Path(tmp)/'profile.json';staged.write_text(json.dumps(record,indent=2)+'\n')
        if profile.read_bytes()!=profile_bytes or sha(target)!=old:raise RuntimeError('Profile changed during staging')
        try:
            if not upgrading:replace(target,inner)
            replace(candidate,target);replace(staged,profile)
            if sha(target)!=new or sha(inner)!=inner_digest:raise RuntimeError('Post-install hash mismatch')
        except Exception:
            replace(backup/target.name,target);replace(backup/profile.name,profile);raise
    shutil.copy2(build/'verification.json',backup/'verification.json')
    print('Installed outer recorder; preserved exact inner provider. Backup:',backup)
if __name__=='__main__':main()
