#!/bin/sh
# Host build of the CubeLinux desktop (real pty + real files under $HOME), screenshots only
cd "$(dirname "$0")/../.."
gcc -O1 -g -DHOST_BUILD -DCUBELINUX -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare -o tools/host/cubelinux_host \
  source/*.c source/apps/*.c tools/host/plat_host.c -lm
