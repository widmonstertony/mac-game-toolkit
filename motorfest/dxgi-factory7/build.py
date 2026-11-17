#!/usr/bin/env python3
"""Build only the Motorfest DXGI fix; reuse the signed engine's winemetal ABI."""
from pathlib import Path
import subprocess,re,concurrent.futures,argparse
ap=argparse.ArgumentParser()
ap.add_argument("--source",type=Path,required=True)
ap.add_argument("--output",type=Path,required=True)
ap.add_argument("--toolchain",type=Path,required=True)
ap.add_argument("--engine",type=Path,required=True)
a=ap.parse_args()
src=a.source.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=src,text=True).strip()=='39d1e4a40121ddfe4f9fc3793bfc336f4491d5b5', 'Wrong Mullion source revision'
tool=a.toolchain.resolve()
engine=a.engine.resolve()
exports=subprocess.check_output([str(tool/'llvm-readobj'),'--coff-exports',str(engine/'lib/mullion/aarch64-windows/winemetal.dll')],text=True)
names=re.findall(r'^  Name: (.+)$',exports,re.M)
(out/'winemetal.def').write_text('LIBRARY winemetal.dll\nEXPORTS\n'+'\n'.join(names)+'\n')
subprocess.run([str(tool/'arm64ec-w64-mingw32-dlltool'),'-d',str(out/'winemetal.def'),'-l',str(out/'libwinemetal.a')],check=True)
files=['src/dxgi/'+f for f in ['dxgi_adapter.cpp','dxgi_factory.cpp','dxgi_output.cpp','dxgi_options.cpp','dxgi.cpp']]
files+=['src/util/'+f for f in ['util_env.cpp','util_string.cpp','util_bloom.cpp','util_md5.cpp','thread.cpp','com/com_guid.cpp','com/com_private_data.cpp','config/config.cpp','log/log.cpp','log/trace.cpp','sha1/sha1.c','sha1/sha1_util.cpp','wsi_monitor_win32.cpp','wsi_window_win32.cpp','wsi_platform_win32.cpp']]
files+=['src/mullion/mullion_format.cpp']
incs=['include','libs','src/util','src/dxgi','src/mullion','src/winemetal','src/airconv']
base=[str(tool/'arm64ec-w64-mingw32-clang++'),'-std=c++20','-fblocks','-DNOMINMAX','-D_WIN32_WINNT=0xa00','-DMULLION_PAGE_SIZE=4096','-O2','-g','-ffunction-sections','-fdata-sections','-Wno-microsoft-exception-spec','-Wno-extern-c-compat']+['-I'+str(src/i) for i in incs]
def build(f):
 o=out/(f.replace('/','_')+'.o');r=subprocess.run(base+['-c',str(src/f),'-o',str(o)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 (out/(o.name+'.log')).write_text(r.stdout)
 return f,r.returncode,r.stdout[-4000:]
results=list(concurrent.futures.ThreadPoolExecutor(max_workers=4).map(build,files))
failed=False
for f,c,log in results:
 if c: print(f,log);failed=True
if failed:raise SystemExit(1)
cmd=[str(tool/'arm64ec-w64-mingw32-clang++'),'-shared','-static','-Wl,--gc-sections','-Wl,--file-alignment=4096','-o',str(out/'dxgi.dll')]+[str(out/(f.replace('/','_')+'.o')) for f in files]+[str(src/'src/dxgi/dxgi.def'),str(out/'libwinemetal.a'),'-lntdll','-lgdi32','-luser32','-ladvapi32','-ldxguid','-lole32','-ldxgi']
subprocess.run(cmd,check=True)
print(out/'dxgi.dll')
