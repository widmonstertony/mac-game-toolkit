#!/usr/bin/env python3
"""Verify the complete runtime provider using private saved game frames, not live gameplay."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--artifacts', required=True, type=Path)
    p.add_argument('--inputs', required=True, type=Path, help='prepare_replay.py output with adjacent 0/1 folders')
    args = p.parse_args(); out = args.artifacts.resolve(); inputs = args.inputs.resolve()
    manifest = json.loads((out/'build-manifest.json').read_text())
    for name, expected in manifest['artifacts'].items():
        if sha(out/name) != expected: raise RuntimeError('Build changed: '+name)
    ui = json.loads((out/'history-native-ui-gpu-verification.json').read_text())
    if not ui['passed'] or ui['manifest_sha256'] != sha(out/'build-manifest.json'):
        raise RuntimeError('Run complete native UI/provider GPU regression first')
    support = Path.home()/'Library/Application Support/protonish/launchers/2483190'
    profile = support/'rosetta-profile'
    spec = importlib.util.spec_from_file_location('fh6profile',support/'rosetta_profile.py')
    module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    env = module.environment(profile);env['WINEDEBUG']='-all'
    wine = str(profile/'engine/bin/wine')
    win = lambda path:'Z:'+str(path).replace('/','\\')
    work = Path(tempfile.mkdtemp(prefix='history-proof-',dir=out))
    def run(name, exe, arguments):
        log = out/(name+'.log')
        with log.open('w') as f:
            result = subprocess.run([wine,str(out/exe),*arguments],cwd=out,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=90)
        contents=log.read_text(errors='replace')
        if result.returncode:raise RuntimeError(name+' failed: '+contents[-3000:])
        return contents
    post=work/'post';post.mkdir()
    log=run('history-post-gpu','history-post-probe.exe',[win(inputs/'0'),win(inputs/'1'),win(post)])
    if 'no outstanding references' not in log or 'Visibility post GPU ms=' not in log:
        raise RuntimeError('Runtime ownership/GPU timing proof incomplete')
    trace=out/'fh6-fg-chain.jsonl';offset=trace.stat().st_size
    provider=work/'provider';provider.mkdir()
    candidate=out/'history-ui-real/framegeneration-history-ui.dll'
    run('history-provider-gpu','replay-capture.exe',[win(candidate),win(inputs/'0'),win(inputs/'1'),win(provider),'provider'])
    rows=[json.loads(x) for x in trace.read_bytes()[offset:].decode().splitlines()]
    active=[x for x in rows if x['event']=='history_post' and x['dispatched']]
    if len(active)!=4 or any(x['event']=='native_ui_passthrough' for x in rows):
        raise RuntimeError('Saved real-pixel replay did not execute the complete provider')
    baseline=work/'baseline';baseline.mkdir()
    original=out/'history-ui-real/framegeneration-original.dll'
    run('history-baseline-gpu','replay-capture.exe',[win(original),win(inputs/'0'),win(inputs/'1'),win(baseline)])
    a=(baseline/'replay-generated.bin').read_bytes();b=(provider/'replay-generated.bin').read_bytes()
    if len(a)!=len(b):raise RuntimeError('Output dimensions changed')
    pixels=range(0,len(a),4)
    changed=sum(any(abs(a[i+k]-b[i+k])>3 for k in range(3)) for i in pixels)
    if changed<100:raise RuntimeError('Provider loaded but no meaningful visibility correction observed')
    # Keep the existing installer evidence keys, with additional required proof.
    receipt=dict(ui)
    receipt.update(kind='runtime history snapshots + complete official provider + real captured-pixel GPU output',
                   log_sha256=sha(out/'native-ui-probe.log'),trace_sha256=sha(out/'native-ui-probe-trace.jsonl'),
                   history_saved_pixel_dispatches=len(active),history_changed_rgb_pixels=changed,
                   history_post_log_sha256=sha(out/'history-post-gpu.log'),
                   history_provider_log_sha256=sha(out/'history-provider-gpu.log'),
                   evidence_directory=work.name,game_visual_verified=False)
    (out/'history-provider-trace.jsonl').write_text('\n'.join(json.dumps(x) for x in rows)+'\n')
    receipt['history_provider_trace_sha256']=sha(out/'history-provider-trace.jsonl')
    (out/'history-gpu-verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'passed':True,'changed_rgb_pixels':changed,'evidence':work.name,'game_visual_verified':False},indent=2))


if __name__=='__main__':main()
