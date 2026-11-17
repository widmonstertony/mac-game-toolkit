#!/usr/bin/env python3
"""Build an isolated general C ABI experiment; never install into a game/engine."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True,help='MetalSharp source/linuxhost at b1ceb881')
p.add_argument('--toolchain',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
expected=json.loads((ROOT/'upstream-files.json').read_text())
for name,digest in expected.items():
    file=a.source/name
    if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest()!=digest:
        raise SystemExit('Pinned upstream source differs: '+name)
out=a.out.resolve(); out.mkdir(parents=True,exist_ok=False)
src=out/'source'; shutil.copytree(a.source,src)
original={str(f.relative_to(src)):hashlib.sha256(f.read_bytes()).hexdigest() for f in src.rglob('*') if f.is_file()}
glibc=src/'glibc.c'; text=glibc.read_text()
anchor='static const struct libc_function *const tables[] =\n{\n'
if text.count(anchor)!=1: raise SystemExit('Unexpected resolver table; source review required')
text=text.replace(anchor,'extern const struct libc_function linuxhost_string_compat_functions[], linuxhost_cxx_support_functions[], linuxhost_time_extra_functions[], linuxhost_jump_extra_functions[];\n'+anchor+'    linuxhost_string_compat_functions, linuxhost_cxx_support_functions, linuxhost_time_extra_functions, linuxhost_jump_extra_functions,\n')
text='extern void *linuxhost_gnu_symbol(const char *);\n'+text
anchor='static char dl_error[256];'
text=text.replace(anchor,anchor+'\nextern int linuxhost_gnu_prepare(char *,size_t);')
anchor='    if (!path) return NULL;\n'
if text.count(anchor)!=1: raise SystemExit('Unexpected dlopen entry')
text=text.replace(anchor,anchor+'    if (!linuxhost_gnu_prepare(dl_error,sizeof(dl_error))) return NULL;\n')
anchor='    if ((module = elf_find( path ))) return module;'
if text.count(anchor)!=1: raise SystemExit('Unexpected dlopen module cache')
text=text.replace(anchor,'''    if ((module = elf_find( path )))
    {
        const struct elf_stats *s=elf_get_stats(module);
        if(s->unresolved || s->unsupported)
        { snprintf(dl_error,sizeof(dl_error),"cached ELF imports incomplete");return NULL; }
        return module;
    }''')
anchor='        const struct elf_stats *stats = elf_get_stats( module );\n'
if text.count(anchor)!=1: raise SystemExit('Unexpected dlopen statistics')
text=text.replace(anchor,anchor+'''        if (stats->unresolved || stats->unsupported)
        {
            snprintf(dl_error,sizeof(dl_error),"incomplete ELF imports: %u unresolved, %u unsupported",stats->unresolved,stats->unsupported);
            return NULL;
        }
''')
anchor='void *linuxhost_resolve( const char *name, void *context )\n{\n'
text=text.replace(anchor,anchor+'    void *gnu=linuxhost_gnu_symbol(name);\n    if(gnu)return gnu;\n')
glibc.write_text(text)
net=src/'libc_net.c'; text=net.read_text()
anchor='const struct libc_function linuxhost_net_functions[] =\n{\n'
if text.count(anchor)!=1: raise SystemExit('Unexpected network table; source review required')
text=text.replace(anchor,'#include "libc-network-extra.inc"\n'+anchor+'    { "sendto", lx_sendto }, { "gethostname", lx_gethostname },\n    { "gethostbyname", lx_gethostbyname }, { "__h_errno_location", lx_h_errno_location },\n')
net.write_text(text)
shutil.copy2(ROOT/'libc-network-extra.inc',src/'libc-network-extra.inc')
io=src/'libc_io.c';text=io.read_text()
anchor='const struct libc_function linuxhost_libc_io_functions[] =\n'
if text.count(anchor)!=1:raise SystemExit('Unexpected stdio table')
text=text.replace(anchor,'#include "libc-stdio-extra.inc"\n#include "libc-mount-extra.inc"\n'+anchor)
table='const struct libc_function linuxhost_libc_io_functions[] =\n{\n'
if text.count(table)!=1:raise SystemExit('Unexpected stdio entries')
io.write_text(text.replace(table,table+'    { "setmntent", lx_setmntent }, { "endmntent", lx_endmntent }, { "getmntent", lx_getmntent },\n'))
shutil.copy2(ROOT/'libc-stdio-extra.inc',src/'libc-stdio-extra.inc')
shutil.copy2(ROOT/'libc-mount-extra.inc',src/'libc-mount-extra.inc')
sources=['elfload.c','glibc.c','syscall.c','process.c','libc.c','libc_io.c','fork.c',
         'libc_thread.c','libc_net.c','libc_misc.c','unwind.c']
cc=str(a.toolchain.absolute()/'bin/x86_64-w64-mingw32-clang')
common=[cc,'-O2','-Wall','-mfsgsbase','-DARRAY_SIZE(a)=(sizeof(a)/sizeof((a)[0]))','-I',str(src),
        *[str(src/f) for f in sources],str(ROOT/'libc-strings.c'),str(ROOT/'libc-cxx-support.c'),str(ROOT/'gnu-runtime.c'),str(ROOT/'libc-time-extra.c'),str(ROOT/'libc-jump-extra.c')]
libs=['-lshell32','-lws2_32','-lbcrypt']
for name,main in [('hosttest.exe',src/'test/hosttest.c'),('strings-probe.exe',ROOT/'probe-driver.c'),
                  ('dependency-probe.exe',ROOT/'dependency-probe.c'),('dlopen-probe.exe',ROOT/'dlopen-probe.c')]:
    subprocess.run([*common,str(main),*libs,'-o',str(out/name)],check=True)
subprocess.run([*common,str(src/'dllmain.c'),'-shared',*libs,'-o',str(out/'linuxhost.dll')],check=True)
subprocess.run([cc,'-O2','-Wall',str(ROOT/'dll-probe.c'),'-o',str(out/'dll-probe.exe')],check=True)
clang=str(a.toolchain.absolute()/'bin/clang')
elf=[clang,'--target=x86_64-unknown-linux-gnu','-O2','-fPIC','-shared','-nostdlib','-fuse-ld=lld',
     '-fno-builtin','-Wl,--hash-style=gnu']
subprocess.run([*elf,str(ROOT/'strings-probe.c'),'-o',str(out/'strings.so')],check=True)
subprocess.run([*elf,str(ROOT/'network-probe.c'),'-o',str(out/'network.so')],check=True)
subprocess.run([*elf,str(ROOT/'stdio-probe.c'),'-o',str(out/'stdio.so')],check=True)
subprocess.run([*elf,str(ROOT/'time-probe.c'),'-o',str(out/'time.so')],check=True)
subprocess.run([*elf,str(ROOT/'mount-probe.c'),'-o',str(out/'mount.so')],check=True)
subprocess.run([*elf,str(ROOT/'jump-probe.c'),'-o',str(out/'jump.so')],check=True)
subprocess.run([*elf,'-fexceptions',str(ROOT/'cxx-probe.cpp'),'-o',str(out/'cxx-probe.so')],check=True)
subprocess.run([*elf,'-ftls-model=global-dynamic',str(src/'test/hello.c'),'-o',str(out/'hello.so')],check=True)
subprocess.run([*elf,'-O1','-fexceptions','-Wl,--eh-frame-hdr',str(src/'test/extras.c'),'-o',str(out/'extras.so')],check=True)
receipt={'upstreamRevision':'b1ceb881dc6443da8772fbae3a906204266e21cc','upstreamFiles':original,
 'ownSources':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in ROOT.glob('*') if f.suffix in ('.c','.cpp','.py','.inc')},
 'outputs':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in out.iterdir() if f.suffix in ('.exe','.dll','.so')},
 'installed':False,'gameplayVerified':False}
(out/'build.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(out)
