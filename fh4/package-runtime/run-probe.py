#!/usr/bin/env python3
"""Test a private Highball kernelbase build, never a production prefix."""
import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--work',type=Path,required=True);p.add_argument('--cc',type=Path,required=True);a=p.parse_args()
w=a.work.resolve();engine=w/'engine-profile';prefix=w/'probe-prefix';fixtures=w/'fixtures'
if not (prefix/'system.reg').is_file() or 'fh4-package-runtime-' not in str(w):raise SystemExit('Isolated diagnostic directory required')
for name in ('main','dep','cwd','missing'): (fixtures/name).mkdir(parents=True,exist_ok=True)
def compile(src,out,*args):
    c=out.with_suffix('.c');c.write_text(src);subprocess.run([str(a.cc),str(c),'-o',str(out),*args],check=True)
probe=fixtures/'main/probe.exe'
subprocess.run([str(a.cc),str(Path(__file__).with_name('probe.c')),'-municode','-o',str(probe)],check=True)
compile('__declspec(dllexport) int leaf(void){return 41;}',fixtures/'dep/leaf.dll','-shared','-Wl,--out-implib,'+str(w/'leaf.a'))
compile('__declspec(dllimport) int leaf(void); __declspec(dllexport) int value(void){return leaf()+1;}',fixtures/'main/head.dll','-shared',str(w/'leaf.a'))
for folder,value in [('main',1),('dep',2)]: compile('__declspec(dllexport) int value(void){return '+str(value)+';}',fixtures/folder/'first.dll','-shared')
compile('__declspec(dllexport) int value(void){return 99;}',fixtures/'cwd/outside.dll','-shared')
compile('__declspec(dllexport) int missing(void){return 0;}',fixtures/'missing/missing.dll','-shared','-Wl,--out-implib,'+str(w/'missing.a'))
compile('__declspec(dllimport) int missing(void); __declspec(dllexport) int value(void){return missing();}',fixtures/'main/broken.dll','-shared',str(w/'missing.a'))
def win(path): return 'Z:'+str(path).replace('/','\\')
fw=engine/'frameworks'
env=dict(os.environ,WINEPREFIX=str(prefix),WINEDEBUG='-all,+loaddll',WINEDLLOVERRIDES='winemenubuilder.exe=d',WINEMSYNC='1',WINEESYNC='0',DYLD_FALLBACK_LIBRARY_PATH=str(fw)+':/usr/lib',DYLD_FALLBACK_FRAMEWORK_PATH=str(fw)+':/System/Library/Frameworks')
for key in tuple(env):
    if key.startswith('FH4_PACKAGE_') or key in ('WINEDLLPATH','WINEDLLPATH_PREPEND'):env.pop(key)
wine=engine/'engine/bin/wine'; results=[]
def run(name,args,e):
    log=w/(name+'.log')
    with log.open('w') as f:
        r=subprocess.run([str(wine),*map(str,args)],env=e,cwd=fixtures/'cwd',stdout=f,stderr=subprocess.STDOUT,timeout=45)
    text=log.read_text(errors='replace')
    result=dict(name=name,exitCode=r.returncode,passMarker=[s for s in text.splitlines() if s.endswith('_PASS') or s.startswith('PACKAGE_CONTEXT_PASS')],logSha256=hashlib.sha256(log.read_bytes()).hexdigest())
    results.append(result); print(json.dumps(result),flush=True)
    return r.returncode
key=r'HKEY_LOCAL_MACHINE\Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\PackageRepository\Packages'
meta={}
for folder,name in [('main','Test.Main'),('dep','Test.Dependency')]:
    full=name+'_1.0.0.0_x64__0123456789abc'; path=fixtures/folder
    run('register-'+folder,['reg','add',key+'\\'+full,'/v','Path','/t','REG_SZ','/d',win(path),'/f'],env)
    meta[folder]=(full,win(path))
run('ordinary',[probe,'--absent'],env)
ctx=dict(env,FH4_PACKAGE_EXE=win(probe),FH4_PACKAGE_APP_ID='Probe',FH4_PACKAGE_FULL=meta['main'][0],FH4_PACKAGE_PATH=meta['main'][1],FH4_PACKAGE_PUBLISHER='CN=Fixture',FH4_PACKAGE_DEP_FULL=meta['dep'][0],FH4_PACKAGE_DEP_PATH=meta['dep'][1],FH4_PACKAGE_DEP_PUBLISHER='CN=Fixture')
run('scoped',[probe],ctx)
run('inherited-unrelated',[probe,'--absent'],dict(ctx,FH4_PACKAGE_EXE=win(fixtures/'main/another.exe')))
run('catalog-mismatch',[probe,'--absent'],dict(ctx,FH4_PACKAGE_PATH=win(fixtures/'cwd')))
result=dict(tests=results,passed=all(x['exitCode']==0 for x in results),gameplayVerified=False,productionChanged=False,kernelbaseSha256=hashlib.sha256((engine/'engine/lib/wine/x86_64-windows/kernelbase.dll').read_bytes()).hexdigest())
(w/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
raise SystemExit(not result['passed'])
