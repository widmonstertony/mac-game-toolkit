#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
: "${LLVM_MINGW_BIN:?Set LLVM_MINGW_BIN to llvm-mingw 20261006 universal bin}"
: "${NETWORK_BUILD_DIR:?Set NETWORK_BUILD_DIR to an empty build directory}"
mkdir -p "$NETWORK_BUILD_DIR"
cd "$NETWORK_BUILD_DIR"
export PATH="$LLVM_MINGW_BIN:${BISON_BIN:-/usr/local/opt/bison/bin}:/usr/bin:/bin:/usr/sbin:/sbin"
curl -fL --retry 2 https://dl.winehq.org/wine/source/11.x/wine-11.18.tar.xz -o wine.tar.xz
curl -fL --retry 2 https://codeload.github.com/gauthierpiarrette/highball-engine/tar.gz/8b315e7 -o engine.tar.gz
shasum -a 256 -c <<'SUMS'
c6282f6d4daecf11f3ab9f9a2b6c598f67be7e28c363ba2cae8a3c44b611e7a4  wine.tar.xz
1718492b3891c938a4236f467797a20cfbcf364c3c41661c12b359a26f2b9fc8  engine.tar.gz
SUMS
tar -xJf wine.tar.xz
tar -xzf engine.tar.gz
for file in highball-engine-8b315e7/patches-arm64/*.patch; do
    patch -d wine-11.18 -s -p1 -F 0 -i "$PWD/$file"
done
patch -d wine-11.18 -p1 -F 0 -i "$here/../src/wine-11.18-udp-netreset.patch"
patch -d wine-11.18 -p1 -F 0 -i "$here/wine-11.18-websocket.patch"
patch -d wine-11.18 -p1 -F 0 -i "$here/wine-11.18-dual-stack.patch"
mkdir build
cd build
../wine-11.18/configure --enable-archs=arm64ec,aarch64,i386 --with-mingw \
    --without-x --without-wayland --without-alsa --without-capi --without-cups --without-dbus \
    --without-fontconfig --without-freetype --without-gnutls --without-gphoto --without-gstreamer \
    --without-krb5 --without-netapi --without-oss --without-pulse --without-sane --without-sdl \
    --without-udev --without-usb --without-v4l2 --without-pcap --without-ffmpeg --without-vulkan \
    --disable-tests CC=/usr/bin/clang CXX=/usr/bin/clang++ \
    arm64ec_CC=arm64ec-w64-mingw32-clang aarch64_CC=aarch64-w64-mingw32-clang \
    i386_CC=i686-w64-mingw32-clang
make -j8 dlls/winhttp/aarch64-windows/winhttp.dll dlls/ws2_32/aarch64-windows/ws2_32.dll
mkdir -p ../payload/aarch64-windows
cp dlls/winhttp/aarch64-windows/winhttp.dll dlls/ws2_32/aarch64-windows/ws2_32.dll ../payload/aarch64-windows/
llvm-strip --strip-debug ../payload/aarch64-windows/*.dll
x86_64-w64-mingw32-clang -O2 -Wall -Wextra -Werror "$here/probe.c" -lwinhttp -lws2_32 -o ../network-probe.exe
shasum -a 256 ../payload/aarch64-windows/*.dll
