#!/usr/bin/env python3
"""Use the existing Heat EA installation for Heat and Unbound, without moving games."""
from datetime import datetime
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply',action='store_true')
    args=parser.parse_args()
    steam=Path.home()/'Library/Application Support/Steam'
    data=steam/'steamapps/compatdata'
    shared=data/'1222680/pfx'
    target=data/'1846380/pfx'
    if not (shared/'drive_c/Program Files/Electronic Arts/EA Desktop').is_dir():
        raise RuntimeError('The existing Heat EA installation is missing')
    if target.is_symlink() and target.resolve()!=shared.resolve():
        raise RuntimeError('Unbound already uses an unexpected shared prefix')
    print('Heat + Unbound: reuse Heat prefix and EA App; retain per-game Steam metadata and game files.')
    if not args.apply:return
    processes=subprocess.check_output(['/bin/ps','-axo','comm='],text=True)
    if any(x in processes.casefold() for x in ('needforspeed','nfsh','eadesktop.exe','eabackgroundservice.exe','easteamlauncher.exe')):
        raise RuntimeError('Close the NFS / EA processes before changing the shared prefix')
    stamp=datetime.now().strftime('%Y%m%d-%H%M%S')
    record=data/'nfs-shared-records'/stamp;record.mkdir(parents=True)
    if not target.is_symlink():
        # Copy only missing NFS save/settings folders before retaining the old prefix.
        for user in (target/'drive_c/users').iterdir():
            for doc in (user/'Documents',user/'My Documents'):
                if not doc.is_dir() or doc.is_symlink():continue
                for source in doc.iterdir():
                    if 'speed' not in source.name.casefold() and 'unbound' not in source.name.casefold():continue
                    dest=shared/'drive_c/users'/user.name/doc.name/source.name
                    if dest.exists():raise RuntimeError('Both prefixes contain the same save folder; resolve before merging')
                    dest.parent.mkdir(parents=True,exist_ok=True)
                    if source.is_dir():shutil.copytree(source,dest,symlinks=True)
                    else:shutil.copy2(source,dest)
        old=target.with_name('pfx.before-ea-share-'+stamp)
        target.rename(old)
        try:target.symlink_to(shared)
        except BaseException:old.rename(target);raise
        (record/'prefix.json').write_text(json.dumps({'shared':str(shared),'alias':str(target),'retained':str(old)},indent=2))
    # lockf locks inodes. Both launchers must lock the same regular file too.
    base_lock=data/'1222680/.notproton-prefix.lock';other_lock=data/'1846380/.notproton-prefix.lock'
    base_lock.touch(exist_ok=True)
    if not other_lock.exists() or other_lock.stat().st_ino!=base_lock.stat().st_ino:
        if other_lock.exists():other_lock.rename(record/'unbound-prefix.lock')
        os.link(base_lock,other_lock)
    print('Both Steam AppIDs resolve to one prefix and share one preparation lock.')
    print('The former Unbound prefix is retained, not deleted.')

if __name__=='__main__':main()
