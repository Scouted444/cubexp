#!/bin/sh
# Fetches the prebuilt RISC-V (rv32ima, no-MMU) Linux 6.8 kernel + BusyBox initramfs
# made by Charles Lohr for mini-rv32ima, and stores it as data/linux.bin.
# Sources: https://github.com/cnlohr/mini-rv32ima and https://github.com/cnlohr/mini-rv32ima-images
set -e
cd "$(dirname "$0")/.."
mkdir -p data
URL=https://github.com/cnlohr/mini-rv32ima-images/raw/master/images/linux-6.8-rc1-rv32nommu-cnl-1.zip
TMP=$(mktemp -d)
if command -v curl >/dev/null 2>&1; then curl -fsSL "$URL" -o "$TMP/img.zip"; else wget -q "$URL" -O "$TMP/img.zip"; fi
if command -v unzip >/dev/null 2>&1; then unzip -o -q "$TMP/img.zip" -d "$TMP"; else python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$TMP/img.zip" "$TMP"; fi
cp "$TMP/Image" data/linux.bin
rm -rf "$TMP"
ls -l data/linux.bin
