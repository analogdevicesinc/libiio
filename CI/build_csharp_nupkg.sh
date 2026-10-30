#!/usr/bin/env bash
# Copyright (c) 2026 Analog Devices, Inc.
#
# SPDX-License-Identifier: MIT

# Packs the libiio C# bindings into a NuGet package.
#
# Usage: build_csharp_nupkg.sh <csproj> <output-dir> [windows-native-dir]
set -euo pipefail

CSPROJ="$1"
OUT_DIR="$2"
WIN_NATIVE_DIR="${3:-}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

VERSION=$(grep -m1 -A3 '^project(' "${ROOT_DIR}/CMakeLists.txt" | grep -oP '(?<=VERSION )[0-9]+\.[0-9]+\.[0-9]+')

GIT_HASH=""
if git -C "${ROOT_DIR}" rev-parse --short HEAD >/dev/null 2>&1; then
    GIT_HASH=$(git -C "${ROOT_DIR}" rev-parse --short HEAD)
fi

# Same scheme as LIBIIO_VERSION (top-level CMakeLists.txt), with the "-"
# SemVer needs before a prerelease identifier instead of CPack's ".".
PACKAGE_VERSION="${VERSION}"
[ -n "${GIT_HASH}" ] && PACKAGE_VERSION="${VERSION}-g${GIT_HASH}"

# Same idea as the CSHARP_DOTNET_TFM CMake option (which only controls the
# normal build, not this package): set the env var to pin a target
# framework, or leave it unset to auto-detect the newest the SDK supports.
TFM="${CSHARP_DOTNET_TFM:-}"
if [ -z "${TFM}" ]; then
    SDK_MAJOR=$(dotnet --version | cut -d. -f1)
    TFM="net${SDK_MAJOR}.0"
fi

dotnet pack "${CSPROJ}" -c Release \
    -o "${OUT_DIR}" \
    -p:Version="${VERSION}.0" \
    -p:PackageVersion="${PACKAGE_VERSION}" \
    -p:LibiioSharpTfm="${TFM}" \
    -p:LibiioSharpWindowsNativeDir="${WIN_NATIVE_DIR}" \
    -p:OutputPath="${OUT_DIR}/obj/"
