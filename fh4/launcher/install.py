#!/usr/bin/env python3
"""Install only the four verified FH4 local compatibility files; no UI launch."""
import argparse
import hashlib
import json
from pathlib import Path
import os


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--receipt',type=Path,required=True)
    p.add_argument('--apply',action='store_true')
    a=p.parse_args(); data=json.loads((a.candidate/'candidate.json').read_text())
    game=Path(data['gameDirectory'])
    allowed={'fh4-uwp-runtime.dll','fh4-corewindow-swapchain.dll','dxgi.dll','ForzaHorizon4.exe.manifest'}
    if {x['file'] for x in data['files']} != allowed:
        raise SystemExit('Unexpected file set')
    if sha(game/'AppxManifest.xml') != data['packageManifestSha256']:
        raise SystemExit('Owned manifest changed')
    for x in data['packageClassServers']:
        if sha(game/x['file'])!=x['sha256']: raise SystemExit('Original server changed')
    for x in data['files']:
        dst=game/x['file']; src=a.candidate/'files'/x['file']
        if sha(src)!=x['candidateSha256']: raise SystemExit('Candidate hash mismatch')
        if x['existingSha256'] is not None or dst.exists() or dst.is_symlink():
            raise SystemExit('Initial install requires absent destinations: '+x['file'])
    if not a.apply:
        print('Four new FH4-only files verified; no changes')
        return
    created=[]
    try:
        for x in data['files']:
            dst=game/x['file']
            with dst.open('xb') as f:
                created.append((dst,x['candidateSha256']))
                f.write((a.candidate/'files'/x['file']).read_bytes())
                f.flush(); os.fsync(f.fileno())
            if sha(dst)!=x['candidateSha256']: raise RuntimeError('Installed hash mismatch')
        with a.receipt.open('x') as f:
            json.dump(dict(installed=True,gameplayVerified=False,files=[dict(path=str(p),sha256=s) for p,s in created]),f,indent=2)
            f.write('\n')
    except BaseException:
        for dst,digest in created:
            if dst.exists() and sha(dst)==digest: dst.unlink()
        raise
    print('Installed four verified files into FH4 only; game was not launched')


if __name__=='__main__': main()
