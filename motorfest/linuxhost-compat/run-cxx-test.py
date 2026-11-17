#!/usr/bin/env python3
"""Exercise owned Linux C++ code against pinned, unmodified GNU runtime DSOs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--engine',type=Path,required=True)
p.add_argument('--prefix',type=Path,required=True)
p.add_argument('--gnu-runtime',type=Path,required=True)
p.add_argument('--client',type=Path,required=True,help='Inspection only; never executed')
a=p.parse_args()
out=a.build.resolve(); prefix=a.prefix.resolve()
if prefix.name!='probe-prefix' or not (prefix/'system.reg').is_file():
    raise SystemExit('Requires an existing isolated probe-prefix')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
receipt=json.loads((out/'build.json').read_text())
for name,digest in receipt['outputs'].items():
    if sha(out/name)!=digest: raise SystemExit('Build hash mismatch: '+name)
expected={
    'libgcc_s.so.1':'753a1cb28fe20a9a0fb75fe6ee2b9b2a9a36041d4ec41817da9bc9a614c4ab00',
    'libstdc++.so.6.0.21':'83846996211e93eaa5f945f631aaa2f7dd4ae98bbd967fac78964668118887ed'}
for name,digest in expected.items():
    if sha(a.gnu_runtime/name)!=digest: raise SystemExit('GNU library hash mismatch: '+name)
env=dict(os.environ,WINEPREFIX=str(prefix),WINEDEBUG='-all',WINEDLLOVERRIDES='winemenubuilder.exe=d')
for name in ('WINEDLLPATH','WINEDLLPATH_PREPEND','PROTON_LINUX_HOST','PROTON_BATTLEYE_RUNTIME','LINUXHOST_AFFINITY_MODE',
             'DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','DYLD_FALLBACK_FRAMEWORK_PATH','LINUXHOST_GNU_RUNTIME'):
    env.pop(name,None)
results={}
for name,args in [('linked-client',[str(a.client.resolve())]),
                  ('gnu-cxx',[str(out/'cxx-probe.so'),'--own-probe'])]:
    log=out/(name+'.log')
    with log.open('w') as f:
        try:
            rc=subprocess.run([str(a.engine/'MacOS/wine'),str(out/'dependency-probe.exe'),
                *[str((a.gnu_runtime/n).resolve()) for n in expected],*args],
                env=env,stdout=f,stderr=subprocess.STDOUT,timeout=45).returncode
        except subprocess.TimeoutExpired: rc=124
    text=log.read_text(errors='replace')
    modules=[{'name':n,'imports':int(i),'unresolved':int(u),'unsupported':int(s)}
        for n,i,u,s in re.findall(r'MODULE (\S+) imports=(\d+) unresolved=(\d+) unsupported=(\d+)',text)]
    results[name]={'exitCode':rc,'modules':modules,'logSha256':sha(log),
        'missing':re.findall(r'^MISSING (.*)$',text,re.M),
        'allDependenciesResolved':len(modules)==3 and all(not m['unresolved'] and not m['unsupported'] for m in modules)}
    if name=='gnu-cxx':
        results[name]['behaviorPassed']=rc==0 and 'GNU_CXX_PASS result=0' in text
    else: results[name]['inspectionCompleted']=rc in (0,1) and len(modules)==3
    print(name,json.dumps(results[name]),flush=True)
result={'tests':results,'gnuRuntimeHashes':expected,'clientSha256':sha(a.client),
    'buildReceiptSha256':sha(out/'build.json'),'clientCodeExecuted':False,
    'installed':False,'gameplayVerified':False}
(out/'cxx-validation.json').write_text(json.dumps(result,indent=2)+'\n')
raise SystemExit(0 if results['gnu-cxx']['behaviorPassed'] and results['linked-client']['inspectionCompleted'] else 1)
