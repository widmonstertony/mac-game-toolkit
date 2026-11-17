#!/usr/bin/env python3
"""Build and verify a real legacy ELF adapter, separately from authenticated play."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for n in ('build','engine','prefix','runtime','gnu-runtime','toolchain','out'):p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parent;out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
if a.prefix.name!='probe-prefix':raise SystemExit('Isolated probe-prefix only')
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
bridge=a.runtime/'v1/lib64/wine/beclient_x64.dll.so';client=a.runtime/'BEClient_x64.so'
if sha(bridge)!='6aa86ad273fb5a533d018a9d7d5ca3b7ca31d9cc6f6b5e5f108fff71c2876cdc' or sha(client)!='c39d4f934d76043ba7183dea0807809bdad3afe6e973a5469f1143c4ca45dd82':raise SystemExit('Unreviewed runtime; refuse to adapt')
build=json.loads((a.build/'build.json').read_text())
for name,digest in build['outputs'].items():
    if sha(a.build/name)!=digest:raise SystemExit('Host build mismatch: '+name)
cc=a.toolchain/'bin/x86_64-w64-mingw32-clang';clang=a.toolchain/'bin/clang'
subprocess.run([str(cc),'-shared',str(root/'legacy-elf-dll.c'),str(root/'legacy-elf-dll.def'),'-o',str(out/'beclient_x64.dll')],check=True)
# Only our own adapter receives Wine's builtin marker. Vendor bytes stay intact.
builtin=bytearray((out/'beclient_x64.dll').read_bytes())
marker_bytes=b'Wine builtin DLL\0';original_size=len(builtin)
if builtin[:2]!=b'MZ' or int.from_bytes(builtin[60:64],'little')<64+len(marker_bytes):raise SystemExit('Unexpected adapter PE header')
builtin[64:64+len(marker_bytes)]=marker_bytes
if len(builtin)!=original_size:raise SystemExit('PE offsets changed')
for arch in ('x86_64-windows','aarch64-windows'):
    d=out/'overlay'/arch;d.mkdir(parents=True)
    (d/'beclient_x64.dll').write_bytes(builtin)
for n in ('legacy-bridge-probe','legacy-abi-driver'):
    subprocess.run([str(cc),str(root/(n+'.c')),'-o',str(out/(n+'.exe'))],check=True)
subprocess.run([str(clang),'--target=x86_64-unknown-linux-gnu','-shared','-fPIC','-nostdlib','-fuse-ld=lld','-mfsgsbase',str(root/'legacy-abi-probe.c'),'-o',str(out/'legacy-abi.so')],check=True)
env={k:v for k,v in os.environ.items() if not k.startswith(('WINE','DYLD_','PROTON_','LINUXHOST_'))}
env.update(WINEPREFIX=str(a.prefix),WINEDEBUG='-all',WINEDLLOVERRIDES='winemenubuilder.exe=d;beclient_x64=n',PROTON_BATTLEYE_RUNTIME=str(a.runtime),LINUXHOST_LEGACY_BRIDGE=str(bridge),LINUXHOST_LEGACY_HOST='Z:'+str(a.build/'linuxhost.dll').replace('/','\\'),LINUXHOST_GNU_RUNTIME=str(a.gnu_runtime),LINUXHOST_NATIVE_HELPER=str(a.build/'native-child-helper'),LINUXHOST_AFFINITY_MODE='windows')
results={}
for case in ('missing-path','owned-win64','vendor-version','builtin-route'):
    runenv=dict(env);exe=out/'legacy-bridge-probe.exe';marker='BRIDGE_VERSION 250';expected=0
    target='Z:'+str(out/'beclient_x64.dll').replace('/','\\')
    if case=='missing-path':runenv.pop('LINUXHOST_LEGACY_BRIDGE');marker='BRIDGE_LOAD_FAILED error=1114';expected=3
    if case=='owned-win64':runenv['LINUXHOST_LEGACY_BRIDGE']=str(out/'legacy-abi.so');exe=out/'legacy-abi-driver.exe';marker='LEGACY_WIN64_ABI_PASS'
    if case=='builtin-route':
        runenv['WINEDLLOVERRIDES']='winemenubuilder.exe=d;beclient_x64=b'
        runenv['WINEDLLPATH']=str(out/'overlay');target='beclient_x64.dll'
    log=out/(case+'.log')
    with log.open('w') as f:
        try:rc=subprocess.run([str(a.engine/'MacOS/wine'),str(exe),target],env=runenv,cwd=out,stdout=f,stderr=subprocess.STDOUT,timeout=30).returncode
        except subprocess.TimeoutExpired:rc=124
    text=log.read_text(errors='replace');results[case]={'exitCode':rc,'passed':rc==expected and marker in text,'logSha256':sha(log)}
    print(case,results[case],text[-1500:],flush=True)
    if rc==124:break
receipt={'tests':results,'allPassed':len(results)==4 and all(v['passed'] for v in results.values()),'originalBridgeSha256':sha(bridge),'originalClientSha256':sha(client),'hostSha256':sha(a.build/'linuxhost.dll'),'adapterSha256':sha(out/'beclient_x64.dll'),'builtinAdapterSha256':sha(out/'overlay/aarch64-windows/beclient_x64.dll'),'sources':{f.name:sha(f) for f in root.glob('legacy-*') if f.is_file()},'runnerSha256':sha(Path(__file__)),'clientConstructorsExecuted':results.get('vendor-version',{}).get('passed',False),'vendorGetVerCalled':results.get('vendor-version',{}).get('passed',False),'vendorInitCalled':False,'installed':False,'gameplayVerified':False}
(out/'legacy-adapter-validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(0 if receipt['allPassed'] else 1)
