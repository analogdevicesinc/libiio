# Copyright (c) 2026 Analog Devices, Inc.
#
# SPDX-License-Identifier: MIT

# CI image for the libiio daqiri: backend (software tests; DAQIRI sockets, DPDK and ibverbs engines).
# Build from the repository root:
#   docker build -f CI/daqiri/Dockerfile -t libiio-daqiri-ci:dev .
# 13.4.0 is not published for ubuntu22.04; 13.4.3 is the newest 13.4.x tag.
# Two stages keep the image small enough for GitHub-hosted runners: DAQIRI is
# built with the full CUDA devel image, the final image carries only what
# libiio needs to configure, link and run against it.
FROM nvidia/cuda:13.4.3-devel-ubuntu22.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config git ca-certificates \
        rdma-core libibverbs-dev \
        librdmacm-dev meson ninja-build libnuma-dev libelf-dev python3-pyelftools curl xz-utils \
    && rm -rf /var/lib/apt/lists/*

COPY CI/daqiri/build-dpdk.sh CI/daqiri/build-daqiri.sh CI/daqiri/DAQIRI_REF \
     CI/daqiri/probe-raw.cpp /tmp/daqiri-ci/
COPY CI/daqiri/patches /tmp/daqiri-ci/patches
RUN /tmp/daqiri-ci/build-dpdk.sh /opt/dpdk
RUN /tmp/daqiri-ci/build-daqiri.sh /opt/daqiri "dpdk ibverbs" /opt/dpdk

FROM nvidia/cuda:13.4.3-runtime-ubuntu22.04
LABEL org.opencontainers.image.description="libiio DAQIRI backend CI image (CUDA runtime + DAQIRI)" \
      org.opencontainers.image.licenses="LGPL-2.1-or-later"

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config git ca-certificates \
        libxml2-dev libzstd-dev libusb-1.0-0-dev libserialport-dev \
        libavahi-client-dev libaio-dev libcdk5-dev bison flex \
        python3 netcat-openbsd lcov rdma-core libibverbs-dev \
        libibverbs1 libmlx5-1 librdmacm1 libnuma1 libnuma-dev libelf1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/dpdk /opt/dpdk
COPY --from=build /opt/daqiri /opt/daqiri
# daqiriConfig.cmake runs find_dependency(CUDAToolkit), which insists on nvcc
# (used only to locate the toolkit; libiio compiles no CUDA).
COPY --from=build /usr/local/cuda/bin/nvcc /usr/local/cuda/bin/nvcc
COPY --from=build /usr/local/cuda/bin/nvcc.profile /usr/local/cuda/bin/nvcc.profile
COPY --from=build /usr/local/cuda/include /usr/local/cuda/include
COPY --from=build /usr/local/cuda/lib64/libcudart* /usr/local/cuda/lib64/
COPY --from=build /usr/local/cuda/lib64/stubs/libcuda.so /usr/local/cuda/lib64/stubs/libcuda.so

# The toolkit ships only libcuda.so in the stubs directory, but libdaqiri.so
# needs libcuda.so.1. The stubs are on LD_LIBRARY_PATH only for GPU-less CI
# (they would shadow a real driver); a GPU job must override LD_LIBRARY_PATH to
# drop the stubs entry.
RUN ln -s libcuda.so /usr/local/cuda/lib64/stubs/libcuda.so.1

ENV PKG_CONFIG_PATH=/opt/daqiri/lib/pkgconfig:/opt/dpdk/lib/x86_64-linux-gnu/pkgconfig \
    CMAKE_PREFIX_PATH=/opt/daqiri:/opt/dpdk \
    LD_LIBRARY_PATH=/opt/daqiri/lib:/opt/dpdk/lib/x86_64-linux-gnu:/usr/local/cuda/lib64:/usr/local/cuda/lib64/stubs
