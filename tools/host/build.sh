#!/bin/sh
# Builds the PC test harness (screenshots only, no window)
cd "$(dirname "$0")/../.."
gcc -O1 -g -DHOST_BUILD -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -o tools/host/cubexp_host \
  source/*.c source/apps/*.c tools/host/plat_host.c -lm -Wno-sign-compare
