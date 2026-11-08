#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/fh6-arm64-build.XXXXXXXX")
: "${LLVM_MINGW_BIN:?Set LLVM_MINGW_BIN to the llvm-mingw 20261006 universal bin directory}"
export PATH="$LLVM_MINGW_BIN:$PATH"
cd "$work"
curl -fL --retry 2 https://dl.winehq.org/wine/source/11.x/wine-11.18.tar.xz -o wine.tar.xz
curl -fL --retry 2 https://codeload.github.com/gauthierpiarrette/highball-engine/tar.gz/8b315e7 -o engine.tar.gz
shasum -a 256 -c <<'SUMS'
c6282f6d4daecf11f3ab9f9a2b6c598f67be7e28c363ba2cae8a3c44b611e7a4  wine.tar.xz
1718492b3891c938a4236f467797a20cfbcf364c3c41661c12b359a26f2b9fc8  engine.tar.gz
SUMS
tar -xJf wine.tar.xz
tar -xzf engine.tar.gz
patches="$work/highball-engine-8b315e7"
cd wine-11.18
for patch_file in "$patches"/patches-arm64/*.patch; do /usr/bin/patch -s -p1 -F 0 -i "$patch_file"; done
for patch_file in "$patches"/patches/{0022,0023}-*.patch; do /usr/bin/patch -p1 -F 1 -i "$patch_file"; done
/usr/bin/patch -p1 -F 0 -i "$here/src/wine-11.18-udp-netreset.patch"
mkdir "$work/build"
cd "$work/build"
../wine-11.18/configure --enable-archs=arm64ec,aarch64,i386 --with-mingw --with-coreaudio \
  --without-x --without-wayland --without-alsa --without-capi --without-cups --without-dbus \
  --without-fontconfig --without-freetype --without-gnutls --without-gphoto --without-gstreamer \
  --without-krb5 --without-netapi --without-oss --without-pulse --without-sane --without-sdl \
  --without-udev --without-usb --without-v4l2 --without-pcap --without-ffmpeg --without-vulkan \
  --disable-tests CC=/usr/bin/clang CXX=/usr/bin/clang++ \
  arm64ec_CC=arm64ec-w64-mingw32-clang aarch64_CC=aarch64-w64-mingw32-clang \
  i386_CC=i686-w64-mingw32-clang
/usr/bin/make -j"$(sysctl -n hw.ncpu)" dlls/winecoreaudio.drv/all dlls/mmdevapi/all
/usr/bin/make -j"$(sysctl -n hw.ncpu)" dlls/ws2_32/aarch64-windows/ws2_32.dll
out="$work/payload"
mkdir -p "$out"/{aarch64-unix,aarch64-windows,i386-windows}
install -m 755 dlls/winecoreaudio.drv/winecoreaudio.so "$out/aarch64-unix/"
for arch in aarch64 i386; do install -m 644 "dlls/mmdevapi/${arch}-windows/mmdevapi.dll" "$out/${arch}-windows/"; done
install -m 644 dlls/ws2_32/aarch64-windows/ws2_32.dll "$out/aarch64-windows/"
codesign --force --sign - "$out/aarch64-unix/winecoreaudio.so"
x86_64-w64-mingw32-clang -shared -O2 -s "$here/src/dialog.c" "$here/src/version.def" -luser32 -o "$out/version.dll"
x86_64-w64-mingw32-clang -Wall -Wextra -Werror "$here/src/audio-probe.c" -lole32 -luuid -o "$work/audio-probe.exe"
printf '%s\n' "$out"
