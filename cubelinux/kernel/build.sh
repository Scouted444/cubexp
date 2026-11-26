#!/bin/sh
# Builds the CubeLinux kernel (with the initramfs from $ROOTFS) for the GameCube.
# Usage: KVER=6.6.63 ROOTFS=/path/to/rootfs sh build.sh   -> out/dtbImage.gamecube
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
KVER=${KVER:-6.6.63}
ROOTFS=${ROOTFS:?set ROOTFS}
CROSS=${CROSS:-powerpc-linux-gnu-}
WORK=${WORK:-$HERE/work}
mkdir -p "$WORK" "$HERE/out"
cd "$WORK"
if [ ! -d linux-$KVER ]; then
  curl -fsSL "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$KVER.tar.xz" | tar xJ
fi
cd linux-$KVER

# --- CubeLinux drivers -------------------------------------------------------
cp "$HERE/cubefb.c" drivers/video/fbdev/cubefb.c
cp "$HERE/cubepad.c" drivers/input/joystick/cubepad.c
grep -q FB_CUBE drivers/video/fbdev/Kconfig || cat >> drivers/video/fbdev/Kconfig <<'KC'

config FB_CUBE
	tristate "GameCube TV framebuffer (CubeLinux)"
	depends on FB && GAMECUBE
	select FB_SYSMEM_HELPERS_DEFERRED
	help
	  Framebuffer for the Nintendo GameCube's video output, using the
	  scan-out buffer set up by the CubeLinux boot loader.
KC
grep -q cubefb drivers/video/fbdev/Makefile || echo 'obj-$(CONFIG_FB_CUBE) += cubefb.o' >> drivers/video/fbdev/Makefile
grep -q INPUT_CUBEPAD drivers/input/joystick/Kconfig || cat >> drivers/input/joystick/Kconfig <<'KC'

config INPUT_CUBEPAD
	tristate "GameCube controllers (CubeLinux)"
	depends on INPUT && GAMECUBE
	help
	  Reports the four GameCube controller ports as gamepads.
KC
grep -q cubepad drivers/input/joystick/Makefile || echo 'obj-$(CONFIG_INPUT_CUBEPAD) += cubepad.o' >> drivers/input/joystick/Makefile
cp "$HERE/gamecube.dts" arch/powerpc/boot/dts/gamecube.dts

# --- initramfs device nodes (the rest comes from the rootfs directory) -------
cat > "$WORK/initramfs.list" <<'IL'
dir /dev 755 0 0
nod /dev/console 600 0 0 c 5 1
nod /dev/null 666 0 0 c 1 3
nod /dev/tty0 620 0 0 c 4 0
nod /dev/tty1 620 0 0 c 4 1
IL

# --- configuration -----------------------------------------------------------
make ARCH=powerpc CROSS_COMPILE=$CROSS gamecube_defconfig
C="scripts/config --file .config"
$C -d PPC_EARLY_DEBUG -d DEBUG_SPINLOCK -d DEBUG_MUTEXES -d SCHED_TRACER -d DMA_API_DEBUG -d FTRACE
$C -d NFS_FS -d ROOT_NFS -d CIFS -d IP_PNP -d SOUND -d KEXEC -d MODULES
$C -e DEVTMPFS -e TMPFS -e UNIX98_PTYS -e VT -e VT_CONSOLE -e FRAMEBUFFER_CONSOLE
$C -e FB -e FB_CUBE -e INPUT_EVDEV -e INPUT_JOYSTICK -e INPUT_CUBEPAD
$C -e LOGO -e LOGO_LINUX_CLUT224 -e FONTS -e FONT_8x16
$C -e BLK_DEV_INITRD --set-str INITRAMFS_SOURCE "$ROOTFS $WORK/initramfs.list"
$C --set-val INITRAMFS_ROOT_UID "$(id -u)" --set-val INITRAMFS_ROOT_GID "$(id -g)" -e INITRAMFS_COMPRESSION_GZIP
$C --set-str LOCALVERSION "-cubelinux" -d LOCALVERSION_AUTO
make ARCH=powerpc CROSS_COMPILE=$CROSS olddefconfig
grep -E "^CONFIG_(FB_CUBE|INPUT_CUBEPAD|FRAMEBUFFER_CONSOLE|DEVTMPFS|LOGO_LINUX_CLUT224)=" .config

make ARCH=powerpc CROSS_COMPILE=$CROSS -j"$(nproc)" dtbImage.gamecube
cp arch/powerpc/boot/dtbImage.gamecube "$HERE/out/"
ls -l "$HERE/out/dtbImage.gamecube"
