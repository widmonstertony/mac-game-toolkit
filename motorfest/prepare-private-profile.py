#!/usr/bin/env python3
"""Prepare a verified Motorfest private runner, without enabling it or launching UI.

The profile includes an exact-AppID hook and rollback plan. Production mutation
is deliberately separate from preparation; all paths/hashes remain reviewable.
"""
import argparse, hashlib, json, shutil, shlex
from pathlib import Path

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('candidate','browser-validation','combined-validation','data','runner','native-client','out'):
        p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();candidate=a.candidate.resolve();out=a.out.resolve();data=a.data.resolve()
    if data.name!='2698940' or data.parent.name!='compatdata' or not (data/'pfx/system.reg').is_file():
        raise SystemExit('Requires the existing Motorfest prefix')
    manifest=json.loads((candidate/'candidate.json').read_text());engine=Path(manifest['engineContents'])
    browser=json.loads(a.browser_validation.read_text());combined=json.loads(a.combined_validation.read_text())
    if manifest['appId']!=2698940 or manifest['active']:raise SystemExit('Inactive Motorfest candidate required')
    if not browser['passed'] or browser['browserLocalControl']:raise SystemExit('Matching native browser GPU pass required')
    if not combined['passed'] or not combined['nativeSteamShimChildVerified']:raise SystemExit('Combined native Steam/DX12 pass required')
    if combined['candidateManifestSha256']!=sha(candidate/'candidate.json'):raise SystemExit('Stale combined receipt')
    if sha(engine/'MacOS/wine')!=manifest['loaderSha256'] or browser['loaderSha256']!=manifest['loaderSha256']:
        raise SystemExit('Engine changed')
    for name,digest in manifest['files'].items():
        if sha(candidate/name)!=digest:raise SystemExit('Candidate file changed: '+name)
    for record in (browser,combined):
        for name,digest in record['rendererFiles'].items():
            if sha(engine/name)!=digest:raise SystemExit('Renderer changed: '+name)
    client=a.native_client.resolve()
    entry=json.loads((candidate/'receipts/entry-validation.json').read_text())
    if not entry.get('passed') or sha(client/'steamclient.dylib')!=entry['nativeClientSha256']:
        raise SystemExit('Verified native Steam client required')
    out.mkdir(parents=True,exist_ok=False)
    shutil.copy2(Path(__file__).with_name('private-runner.py'),out/'launch.py')
    config=dict(enabled=False,appId=2698940,data=str(data),candidate=str(candidate),engine=str(engine),nativeClient=str(client),
        gameplayVerified=False,browserVisibleVerified=False,renderer='Mullion',cpu='FEX',
        candidateSha256=sha(candidate/'candidate.json'),sharedRunner=str(a.runner.resolve()),sharedRunnerSha256=sha(a.runner),
        checks={'browserGpu':sha(a.browser_validation),'steamDx12':sha(a.combined_validation)})
    (out/'profile.json').write_text(json.dumps(config,indent=2)+'\n')
    q=shlex.quote
    # This hook leaves every other AppID, data path and unsupported verb on the
    # byte-identical original runner. Merely preparing it changes no routing.
    hook=f'''# MOTORFEST_PRIVATE_PROFILE_BEGIN
case "$1" in
  run|waitforexitandrun|getcompatpath)
    if [ "${{STEAM_COMPAT_APP_ID:-${{SteamAppId:-}}}}" = 2698940 ] && [ "$STEAM_COMPAT_DATA_PATH" = {q(str(data))} ]; then
      exec /usr/bin/python3 {q(str(out/'launch.py'))} "$@"
    fi ;;
esac
# MOTORFEST_PRIVATE_PROFILE_END
'''
    (out/'runner-hook.sh').write_text(hook)
    shutil.copy2(a.runner,out/'original-shared-runner')
    plan={'state':'prepared-not-enabled','productionChanged':False,'profileSha256':sha(out/'profile.json'),
        'hookSha256':sha(out/'runner-hook.sh'),'launcherSha256':sha(out/'launch.py'),
        'pending':['Stop only Motorfest prefix and take APFS clone backup',
          'Install native Steam overlay and registered original-client adapter in Motorfest prefix',
          'Archive browser-local Highball DXMT DLLs and select engine-matching builtin D3D11 for Ubisoft',
          'Enable exact-AppID hook after lifecycle verification; preserve all other runner bytes',
          'Actual Ubisoft window, protected Motorfest startup, game exit and gameplay verification'],
        'rollback':'Remove exact hook and restore the prefix clone; preserve a clone of post-test prefix first so new data is not discarded.'}
    (out/'install-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    print('Prepared Motorfest private profile:',out)
if __name__=='__main__':main()
