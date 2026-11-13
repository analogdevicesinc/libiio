#!/bin/sh
# Copyright (c) 2026 Analog Devices, Inc.
#
# SPDX-License-Identifier: MIT
#
# Build and install DPDK 25.11 with the DAQIRI patches into a prefix.
# Usage: build-dpdk.sh <absolute-prefix>
# Needs: meson ninja pkg-config libnuma-dev libelf-dev python3-pyelftools
#        libibverbs-dev (for the mlx5 PMD). The prefix must be writable.
set -eu

if [ $# -ne 1 ]; then
	echo "usage: $0 <absolute-prefix>" >&2
	exit 2
fi
prefix=$1
case "$prefix" in
/*) ;;
*) echo "prefix must be an absolute path" >&2; exit 2 ;;
esac

here=$(cd "$(dirname "$0")" && pwd)
ref=$(cat "$here/DAQIRI_REF")
tmp=${TMPDIR:-/tmp}
dpdk_version=25.11
dsrc=$tmp/dpdk-$dpdk_version
daqiri_src=$tmp/daqiri-src-$ref

# Same driver trim as DAQIRI's docs/tutorials/bare-metal-cmake-build.md.
disable_drivers='baseband/*,bus/ifpga/*,common/cpt,common/dpaax,common/iavf,common/octeontx,common/octeontx2,crypto/nitrox,net/ark,net/atlantic,net/avp,net/axgbe,net/bnx2x,net/bnxt,net/cxgbe,net/e1000,net/ena,net/enic,net/fm10k,net/hinic,net/hns3,net/i40e,net/ixgbe,vdpa/ifc,net/igc,net/liquidio,net/mana,net/netvsc,net/nfp,net/qede,net/sfc,net/thunderx,net/vdev_netvsc,net/vmxnet3,regex/octeontx2'

if [ ! -d "$daqiri_src/.git" ]; then
	git clone https://github.com/NVIDIA/daqiri "$daqiri_src"
fi
git -C "$daqiri_src" checkout "$ref"

# Always start from a pristine tree so the patches apply exactly once.
rm -rf "$dsrc"
tarball=$tmp/dpdk-$dpdk_version.tar.xz
if [ ! -f "$tarball" ]; then
	curl -fsSL "https://fast.dpdk.org/rel/dpdk-$dpdk_version.tar.xz" -o "$tarball.part"
	mv "$tarball.part" "$tarball"
fi
tar xf "$tarball" -C "$tmp"

cd "$dsrc"
git apply \
	--exclude=.mailmap \
	--exclude='doc/guides/rel_notes/release_26_03.rst' \
	"$daqiri_src/dpdk_patches/dmabuf.patch"
git apply "$daqiri_src/dpdk_patches/dpdk.nvidia.patch"

meson setup build \
	--prefix="$prefix" \
	-Dtests=false \
	-Dplatform=generic \
	-Denable_docs=false \
	-Ddisable_drivers="$disable_drivers"
ninja -C build -j"$(nproc)"
meson install -C build

ver=$(PKG_CONFIG_PATH=$prefix/lib/x86_64-linux-gnu/pkgconfig pkg-config --modversion libdpdk)
echo "$ver"
case "$ver" in
25.11 | 25.11.*) ;;
*) echo "unexpected DPDK version $ver (want 25.11.x)" >&2; exit 1 ;;
esac
