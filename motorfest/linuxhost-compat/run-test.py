#!/usr/bin/env python3
"""Run headless ABI probes and inspect dependencies, without running client code."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--engine',type=Path,required=True,help='Signed MetalSharp .app Contents')
p.add_argument('--prefix',type=Path,required=True)
p.add_argument('--runtime',type=Path,required=True,help='Already downloaded official client/bridge package')
a=p.parse_args(); out=a.build.resolve(); prefix=a.prefix.resolve()
if prefix.name!='probe-prefix' or not (prefix/'system.reg').is_file():
    raise SystemExit('Requires an existing isolated probe-prefix; never a game prefix')
build=json.loads((out/'build.json').read_text())
for name,digest in build['outputs'].items():
    if hashlib.sha256((out/name).read_bytes()).hexdigest()!=digest: raise SystemExit('Hash mismatch: '+name)
runtime=a.runtime.resolve()
client=runtime/'BEClient_x64.so'; bridge=runtime/'v1/lib64/wine/beclient_x64.dll.so'
if not client.is_file() or not bridge.is_file(): raise SystemExit('Client/bridge missing')
env=dict(os.environ,WINEPREFIX=str(prefix),WINEDEBUG='-all',WINEDLLOVERRIDES='winemenubuilder.exe=d')
for name in ('WINEDLLPATH','WINEDLLPATH_PREPEND','PROTON_LINUX_HOST','PROTON_BATTLEYE_RUNTIME',
             'DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','DYLD_FALLBACK_FRAMEWORK_PATH'):
    env.pop(name,None)
tests=[('strings','strings-probe.exe',[str(out/'strings.so')]),
       *[(f'overflow-{i}','strings-probe.exe',[str(out/'strings.so'),str(i)]) for i in range(5)],
       ('hello','hosttest.exe',['hello',str(out/'hello.so')]),
       ('extras','hosttest.exe',['extras',str(out/'extras.so')]),
       ('inspect','hosttest.exe',['inspect',str(bridge),str(client)])]
results={}
for name,exe,args in tests:
    log_path=out/(name+'.log')
    with log_path.open('w') as f:
        try:
            rc=subprocess.run([str(a.engine/'MacOS/wine'),str(out/exe),*args],env=env,cwd=out,
                stdout=f,stderr=subprocess.STDOUT,timeout=45).returncode
        except subprocess.TimeoutExpired: rc=124
    log=log_path.read_text(errors='replace')
    if name.startswith('overflow'):
        passed=rc in (9,0xc0000409) and 'LINUXHOST_FORTIFY_REJECT before_write=true' in log and 'FORTIFY_UNEXPECTED_RETURN' not in log
    elif name=='strings': passed=rc==0 and 'STRING_ABI_PASS result=0' in log
    elif name=='inspect': passed=rc in (0,1) and 'unsupported relocations' in log
    else: passed=rc==0 and '0 failure(s)' in log
    results[name]={'exitCode':rc,'probePassed':passed,'logSha256':hashlib.sha256(log_path.read_bytes()).hexdigest()}
    if name=='inspect':
        results[name]['unresolvedCounts']=[int(v) for v in re.findall(r'(\d+) unresolved,',log)]
        results[name]['allDependenciesResolved']=rc==0
    print(name,results[name],flush=True)
receipt={'tests':results,'generalAbiTestsPassed':all(v['probePassed'] for k,v in results.items() if k!='inspect'),
         'runtimeFiles':{str(f.relative_to(runtime)):hashlib.sha256(f.read_bytes()).hexdigest() for f in (bridge,client)},
         'buildReceiptSha256':hashlib.sha256((out/'build.json').read_bytes()).hexdigest(),
         'clientCodeExecuted':False,'installed':False,'gameplayVerified':False}
(out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(0 if receipt['generalAbiTestsPassed'] and results['inspect']['probePassed'] else 1)
