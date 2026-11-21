#!/usr/bin/env bash
# curl 7.80.0 for the PS4 against the project-local mbedTLS 2.28.10 (toolchain/tls-stack/ps4/prefix).
# Static libraries only (libcurl.a), same toolchain target and sysroot rules as scripts/build-ffmpeg-tls-3.sh.
# The sysroot is used read-only; the output goes to toolchain/tls-stack/ps4/prefix-curl/{include,lib}.
set -euo pipefail
export LC_ALL=C

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TC="$REPO/toolchain"
SYSROOT="/root/zimtv-arquivos/opt/pacbrew/ps4/openorbis"
SYSROOT_DIR="${SYSROOT%/opt/pacbrew/ps4/openorbis}"
TLS_PREFIX="$TC/tls-stack/ps4/prefix"
SRC_ROOT="$TC/src/curl-tls3"
PREFIX="$TC/tls-stack/ps4/prefix-curl"
ARCHIVE="$TC/downloads/curl-7.80.0.tar.xz"
ARCHIVE_SHA256="a132bd93188b938771135ac7c1f3ac1d3ce507c1fcbef8c471397639214ae2ab"

[[ -f "$SYSROOT/usr/lib/libz.a" ]] || { echo "zlib missing in $SYSROOT" >&2; exit 1; }
[[ -f "$TLS_PREFIX/lib/libmbedtls.a" ]] || { echo "TLS prefix missing: $TLS_PREFIX (scripts/build-tls-stack.sh first)" >&2; exit 1; }

if [[ ! -f "$ARCHIVE" ]]; then
    mkdir -p "$TC/downloads"
    echo "downloading curl 7.80.0 source"
    curl -fsSL --retry 3 -o "$ARCHIVE.part" https://curl.se/download/curl-7.80.0.tar.xz
    mv "$ARCHIVE.part" "$ARCHIVE"
fi
echo "$ARCHIVE_SHA256  $ARCHIVE" | sha256sum -c --quiet - || { echo "curl tarball checksum mismatch" >&2; exit 1; }

rm -rf "$SRC_ROOT" "$PREFIX"
mkdir -p "$SRC_ROOT"
tar xJf "$ARCHIVE" -C "$SRC_ROOT"
# RESTORE BUILD (default): apply exactly the proven TLSDIAG5 patch (the configuration that produced POSTERS-TEST,
# which is stable on the console across multiple screens and sessions). The TLSDIAG11 patch (observe-only
# cert fingerprinting, verify untouched) is preserved in scripts/patches/curl-7.80.0-tlsdiag11.patch and can
# be applied to produce a diagnostic build; it is NOT applied by default. The TLSDIAG7/9/10 patches are
# preserved as historical diagnostics but are NOT applied: their peer-certificate-chain dump crashes the
# app on the PS4 (every HTTPS verify failure ends the session with CE-34878-0 at the same log line).
if [[ ! -f "$REPO/scripts/patches/curl-7.80.0-tlsdiag5.patch" ]]; then
    echo "FATAL: tlsdiag5 patch missing" >&2
    exit 1
fi
echo "applying TLS diagnostics patch v5 (proven, logging only, no behavior change)"
patch -p1 -d "$SRC_ROOT/curl-7.80.0" < "$REPO/scripts/patches/curl-7.80.0-tlsdiag5.patch"
cd "$SRC_ROOT/curl-7.80.0"
# pkg-config via a wrapper so configure / libcurl build see the sysroot as the only library source
export LT_SYSROOT="$SYSROOT"

cat > "$SRC_ROOT/openorbis-pkg-config" <<EOF2
#!/bin/sh
PKG_CONFIG_DIR= PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$SYSROOT_DIR" exec /data/data/com.termux/files/usr/bin/pkg-config --static "\$@"
EOF2
chmod +x "$SRC_ROOT/openorbis-pkg-config"

export CC="$TC/tls-stack/ps4/ps4-clang"
export AR="/data/data/com.termux/files/usr/bin/llvm-ar"
export RANLIB="/data/data/com.termux/files/usr/bin/llvm-ranlib"
export NM="/data/data/com.termux/files/usr/bin/llvm-nm"
export STRIP="/data/data/com.termux/files/usr/bin/llvm-strip"
export LT_SYSROOT="$SYSROOT"
export PKG_CONFIG="$SRC_ROOT/openorbis-pkg-config"
CURL_CACHE="$SRC_ROOT/config.cache-ps4.sh"
cat > "$CURL_CACHE" <<EOFCACHE
curl_cv_func_recv=yes
curl_cv_func_recv_args=ssize_t,void *,size_t,int,ssize_t
curl_cv_func_send=yes
curl_cv_func_send_args=ssize_t,const void *,size_t,int,ssize_t
EOFCACHE
./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd12 --cache-file="$CURL_CACHE" --build=aarch64-linux-android \
    --with-mbedtls="$TLS_PREFIX" --with-zlib="$SYSROOT/usr" \
    --disable-shared --enable-static \
    --without-brotli --without-zstd --without-nghttp2 --without-libpsl --without-libidn2 \
    --without-libssh2 --without-librtmp --without-libnettle \
    --disable-ldap --disable-ldaps \
    --disable-ftp --disable-file --disable-dict --disable-imap --disable-pop3 --disable-rtmp \
    --disable-smb --disable-smtp --disable-telnet --disable-tftp --disable-rtsp --disable-gopher \
    --disable-mqtt --disable-tftp --disable-doh \
    --disable-manual --disable-docs --disable-silent-rules \
    > "$SRC_ROOT/configure.log" 2>&1 || { tail -40 "$SRC_ROOT/configure.log"; exit 1; }
make -j "$(getconf _NPROCESSORS_ONLN)" > "$SRC_ROOT/make.log" 2>&1 || { tail -40 "$SRC_ROOT/make.log"; exit 1; }
make install > "$SRC_ROOT/install.log" 2>&1
if grep -l -a -F "$REPO" "$PREFIX"/lib/*.a 2>/dev/null; then echo "host path embedded in the libraries" >&2; exit 1; fi
echo "libcurl installed to $PREFIX"
