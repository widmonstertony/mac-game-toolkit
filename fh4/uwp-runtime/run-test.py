#!/usr/bin/env python3
"""Execute only lifecycle.exe in an explicitly supplied, existing test prefix."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--engine', type=Path, required=True, help='Highball engine root')
p.add_argument('--prefix', type=Path, required=True)
a = p.parse_args()
out = a.build.resolve()
prefix = a.prefix.resolve()
if prefix.name != 'probe-prefix' or not (prefix / 'system.reg').is_file():
    raise SystemExit('Requires an existing isolated probe-prefix; never a game bottle')
if not (out / 'build.json').is_file():
    raise SystemExit('Missing build receipt')
receipt = json.loads((out / 'build.json').read_text())
for name in ('fh4-uwp-runtime.dll', 'lifecycle.exe'):
    if hashlib.sha256((out / name).read_bytes()).hexdigest() != receipt['files'][name]:
        raise SystemExit('Build hash mismatch: '+name)
fw = a.engine / 'frameworks'
env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEMSYNC='1',
           WINEESYNC='0', MVK_CONFIG_LOG_LEVEL='0', WINEDLLOVERRIDES='winemenubuilder.exe=d',
           DYLD_FALLBACK_LIBRARY_PATH=str(fw)+':/usr/lib',
           DYLD_FALLBACK_FRAMEWORK_PATH=str(fw)+':/System/Library/Frameworks')
with (out / 'run.log').open('w') as f:
    result = subprocess.run([str(a.engine / 'engine/bin/wine'), str(out / 'lifecycle.exe')],
                            cwd=out, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=60)
log = (out / 'run.log').read_text(errors='replace')
passed = result.returncode == 0 and 'LIFECYCLE_PASS' in log
(out / 'result.json').write_text(json.dumps(dict(passed=passed, exitCode=result.returncode,
    testedDllSha256=receipt['files']['fh4-uwp-runtime.dll'],
    testedExeSha256=receipt['files']['lifecycle.exe'],
    logSha256=hashlib.sha256((out / 'run.log').read_bytes()).hexdigest(),
    gameplayVerified=False, activationEventSubsetVerified=passed, activationRoutingVerified=passed), indent=2)+'\n')
print(log[-3000:])
raise SystemExit(0 if passed else 1)
