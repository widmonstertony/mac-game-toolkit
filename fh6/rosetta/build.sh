#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
: "${FH6_BUILD_DIR:?Set FH6_BUILD_DIR to a new writable build directory}"
: "${WINE_CROSSCC:?Set WINE_CROSSCC to the x86_64-w64-mingw32-gcc compiler (not clang)}"
[[ "$WINE_CROSSCC" == *x86_64-w64-mingw32-gcc ]] || { echo 'Wine expects the GCC target in the compiler filename'; exit 1; }
mkdir -p "$FH6_BUILD_DIR"
cd "$FH6_BUILD_DIR"
curl -fL --retry 2 https://github.com/MathiasKowoll/MacGameFix/releases/download/v1.6.2/wine-source-crossover-26.3.tar.xz -o wine.tar.xz
curl -fL --retry 2 https://codeload.github.com/gauthierpiarrette/highball-engine/tar.gz/8b315e7 -o highball.tar.gz
shasum -a 256 -c <<'SUMS'
cdbe7426f4247bc4a3a87444a30211e344112ec5d723e75d7141e56bfdb58d9d  wine.tar.xz
1718492b3891c938a4236f467797a20cfbcf364c3c41661c12b359a26f2b9fc8  highball.tar.gz
SUMS
tar -xJf wine.tar.xz
tar -xzf highball.tar.gz
source_dir="$PWD/wine-source-crossover-26.3"
for p in highball-engine-8b315e7/patches/*.patch; do
    case "$(basename "$p")" in 0013-*|0025-*|0026-*|0027-*|0028-*) continue;; esac
    patch -d "$source_dir" -p1 -F0 -i "$PWD/$p"
done
for p in wine11-highball-exception-chain.patch wine11-initializer-reentry.patch wine11-utf8-dialog.patch wine11-websocket.patch wine11-dual-stack.patch; do
    patch -d "$source_dir" -p1 -F0 -i "$here/$p"
done
python3 - "$source_dir" <<'PY'
from pathlib import Path
import sys
r=Path(sys.argv[1]);p=r/'dlls/ws2_32/socket.c';s=p.read_text()
assert s.count('    case SIO_UDP_CONNRESET:\n')==1
s=s.replace('IOCTL_NAME(SIO_UDP_CONNRESET);','IOCTL_NAME(SIO_UDP_CONNRESET);\n        IOCTL_NAME(SIO_UDP_NETRESET);').replace('    case SIO_UDP_CONNRESET:\n','    case SIO_UDP_CONNRESET:\n    case SIO_UDP_NETRESET:\n').replace('FIXME( "SIO_UDP_CONNRESET stub\\n" );','FIXME( "%s stub\\n", debugstr_wsaioctl( code ) );');p.write_text(s)
p=r/'include/mswsock.h';s=p.read_text().replace('#define SIO_UDP_CONNRESET               _WSAIOW(IOC_VENDOR, 12)','#define SIO_UDP_CONNRESET               _WSAIOW(IOC_VENDOR, 12)\n#define SIO_UDP_NETRESET                _WSAIOW(IOC_VENDOR, 15)').replace('#define WS_SIO_UDP_CONNRESET            _WSAIOW(WS_IOC_VENDOR, 12)','#define WS_SIO_UDP_CONNRESET            _WSAIOW(WS_IOC_VENDOR, 12)\n#define WS_SIO_UDP_NETRESET             _WSAIOW(WS_IOC_VENDOR, 15)');p.write_text(s)
PY
mkdir build
cd build
sdk=$(xcrun --show-sdk-path)
export CC="/usr/bin/clang -arch x86_64 -isysroot $sdk -mmacosx-version-min=14.0"
export CXX="/usr/bin/clang++ -arch x86_64 -isysroot $sdk -mmacosx-version-min=14.0"
export LDFLAGS=-Wl,-headerpad_max_install_names
export ac_cv_func_pipe2=no
export CROSSCC="$WINE_CROSSCC" x86_64_CC="$WINE_CROSSCC"
"$source_dir/configure" --host=x86_64-apple-darwin --enable-archs=x86_64 --with-mingw \
 --without-freetype --without-x --without-wayland --without-gstreamer --without-vulkan --without-opengl \
 --without-cups --without-dbus --without-fontconfig --without-gnutls --without-krb5 --without-gssapi \
 --without-opencl --without-pcap --without-usb --without-v4l2 --without-sane --without-capi \
 --without-gphoto --without-netapi --without-pulse --without-alsa --without-oss --without-udev \
 --without-unwind --without-ffmpeg --without-inotify
make -j4 dlls/ntdll/ntdll.so dlls/ntdll/x86_64-windows/ntdll.dll \
 dlls/kernelbase/x86_64-windows/kernelbase.dll \
 dlls/winhttp/x86_64-windows/winhttp.dll dlls/ws2_32/x86_64-windows/ws2_32.dll
"$WINE_CROSSCC" -shared -nostdlib -nodefaultlibs -Wl,--entry,DllMain -O2 \
 "$here/steamclient-x64.c" "$here/steamclient-x64.def" -lkernel32 -o steamclient64.dll
# Do not activate this build before the instruction, Steam, network and GPU checks pass.
echo 'Build complete; compatibility validation and FH6-only installation are separate steps.'
