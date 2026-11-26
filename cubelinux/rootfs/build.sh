#!/bin/sh
# Builds the CubeLinux root filesystem: static BusyBox + the CubeXP desktop (cubede) for PowerPC.
# Usage: sh build.sh  -> out/  (pass this directory to the kernel build as ROOTFS)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$HERE/../.." && pwd)
BBVER=${BBVER:-1.36.1}
CROSS=${CROSS:-powerpc-linux-gnu-}
WORK=${WORK:-$HERE/work}
OUT=${OUT:-$HERE/out}
CFLAGS_PPC="-O2 -mcpu=750 -static"
rm -rf "$OUT"; mkdir -p "$WORK" "$OUT"

# --- BusyBox -----------------------------------------------------------------
cd "$WORK"
[ -d busybox-$BBVER ] || curl -fsSL "https://busybox.net/downloads/busybox-$BBVER.tar.bz2" | tar xj
cd busybox-$BBVER
make CROSS_COMPILE=$CROSS defconfig >/dev/null
sed -i -e 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' \
       -e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' \
       -e 's/^CONFIG_FEATURE_TC_INGRESS=y/# CONFIG_FEATURE_TC_INGRESS is not set/' .config
yes "" | make CROSS_COMPILE=$CROSS oldconfig >/dev/null
make CROSS_COMPILE=$CROSS -j"$(nproc)" CONFIG_EXTRA_CFLAGS="-mcpu=750" busybox
make CROSS_COMPILE=$CROSS CONFIG_PREFIX="$OUT" install >/dev/null
file busybox

# --- the desktop ---------------------------------------------------------------
mkdir -p "$OUT/usr/bin"
SRC=$(ls "$TOP"/source/*.c "$TOP"/source/apps/*.c)
${CROSS}gcc $CFLAGS_PPC -DCUBELINUX -I"$TOP/source" -Wall -Wno-format-truncation -Wno-unused-parameter \
    -o "$OUT/usr/bin/cubede" $SRC -lm
${CROSS}strip "$OUT/usr/bin/cubede"
file "$OUT/usr/bin/cubede"

# --- skeleton ------------------------------------------------------------------
cp -a "$HERE/etc" "$OUT/"
cp -a "$HERE/usr" "$OUT/"
mkdir -p "$OUT/proc" "$OUT/sys" "$OUT/dev" "$OUT/tmp" "$OUT/run" "$OUT/root" "$OUT/var/log" "$OUT/mnt" "$OUT/home/player"
chmod 1777 "$OUT/tmp"
chmod 755 "$OUT/etc/init.d/rcS" "$OUT/usr/bin/cubelinux-session"
cp "$TOP/README.md" "$OUT/home/player/README.txt" 2>/dev/null || true
du -sh "$OUT"
