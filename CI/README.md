# CI/CD Overview

libiio uses GitHub Actions for continuous integration and deployment. The CI
builds the library across a wide range of platforms and architectures, runs
tests, and handles artifact publishing for releases.

## Branches

| Branch | Purpose |
|--------|---------|
| `main` | libiio **v1** development (new API, incompatible with v0) |
| `libiio-v0` | libiio **v0.x** maintenance (stable API, v0.26 and earlier compatible) |
| `next_stable` | Staging for the next stable release |
| `dev` | Active development and experimentation |
| `staging/*` | Feature integration branches |

Both `main` and `libiio-v0` trigger the full build matrix on every push.
Pull requests targeting `main`, `next_stable`, or `dev` also trigger builds.

## Workflow Architecture

The CI uses an **orchestrator + reusable workflows** pattern:

```
build.yml                  <-- orchestrator (triggers, config, deploy)
  |
  +-- _linux-builds.yml    <-- 9 Linux distros in Docker containers
  +-- _windows-builds.yml  <-- MSVC, MinGW, and Windows installer
  +-- _macos-builds.yml    <-- macOS 14, 15, and latest (all ARM64)
  +-- _arm-builds.yml      <-- ARM/cross-arch via QEMU (5 configs)
  +-- _mcu-builds.yml      <-- ARM Cortex-M4 cross-compile
```

Reusable workflows (prefixed with `_`) are called by `build.yml` and cannot
be triggered independently. The orchestrator computes shared configuration
(build type, branch flags) and passes it to each reusable workflow as inputs.

### Other Workflows

These run independently on push/PR and are not part of the build matrix:

| Workflow | Purpose |
|----------|---------|
| `tests.yml` | Unit tests with code coverage |
| `doc.yml` | Documentation build, link check, and GitHub Pages deployment |
| `freebsd.yml` | FreeBSD build verification |
| `python_bindings.yml` | Python wheel build and import test |
| `zephyr.yml` | Zephyr RTOS build with Twister |
| `codespell.yml` | Spelling checks |
| `vale.yml` | Documentation prose linting |

## Build Matrix

### Linux (`_linux-builds.yml`)

Builds inside pre-configured Docker containers (`tfcollins/libiio_*-ci`):

| Distro | Artifact |
|--------|----------|
| Ubuntu 20.04, 22.04, 24.04, 26.04 | `.deb` + `.tar.gz` |
| Debian 11 (Bullseye), 12 (Bookworm) | `.deb` + `.tar.gz` |
| Fedora 34 | `.rpm` + `.tar.gz` |
| openSUSE 15.4 | `.rpm` + `.tar.gz` |

Fedora 28 also builds but runs via `docker run` instead of the `container:`
directive because its glibc (2.27) is too old for the Node.js 24 runtime that
GitHub Actions requires inside containers.

On PRs targeting `main`, the Ubuntu 22.04 build also runs:
- **Kernel check** -- verifies IIO channel types/modifiers match the upstream Linux kernel
- **README_BUILD check** -- ensures all CMake options are documented

### Windows (`_windows-builds.yml`)

Three jobs:

1. **MSVC builds** -- Visual Studio 2022 and 2026, produces DLLs, `.exe` utilities,
   C# bindings, and runs C# integration tests (VS2022 only)
2. **MinGW builds** -- Cross-compiles via MSYS2/MinGW-w64
3. **Installer** -- Generates `libiio-setup.exe` using Inno Setup (depends on MSVC builds)

### macOS (`_macos-builds.yml`)

Builds on GitHub-hosted ARM64 (Apple Silicon) runners:

| Runner | Artifact |
|--------|----------|
| `macos-14` | `macOS-14-arm64` |
| `macos-15` | `macOS-15-arm64` |
| `macos-latest` | `macOS-latest-arm64` |

Each produces a `.pkg` installer and a `.tar.gz` archive.

### ARM / Cross-Architecture (`_arm-builds.yml`)

Uses QEMU user-space emulation to cross-compile in Docker:

| Architecture | Image | Artifact |
|-------------|-------|----------|
| aarch64 | Ubuntu 20.04 | `Ubuntu-arm64v8` |
| arm (32-bit) | Ubuntu 20.04 | `Ubuntu-arm32v7` |
| ppc64le | Ubuntu 20.04 | `Ubuntu-ppc64le` |
| s390x | Ubuntu 20.04 | `Ubuntu-x390x` |
| arm (32-bit) | Debian Bookworm | `Debian12-arm` |

### MCU (`_mcu-builds.yml`)

Cross-compiles for ARM Cortex-M4 using `gcc-arm-none-eabi`. Build-only (no
artifacts uploaded) -- verifies the tinyiiod embedded target compiles.

## Build Type

The build type is determined automatically:

- **Tags** (`v*`): `Release`
- **Everything else**: `RelWithDebInfo`

## Deployment

Deployment jobs run only after all builds succeed:

### Artifact Check

Runs on `main` and tag pushes. Downloads all artifacts and validates them
against `artifact_manifest.txt` (generated from `artifact_manifest.txt.cmakein`
by CMake).

### SW Downloads

Runs on `main` only. Pushes artifacts via SCP to the Analog Devices software
downloads server. Requires the `SSH_PRIVATE_KEY` and `SERVER_ADDRESS` secrets.

### GitHub Release

Runs on tag pushes (`v*`) only. Creates a **draft** GitHub release with all
build artifacts attached.

## Helper Scripts

Build logic lives in `CI/scripts/` and `CI/` scripts, called from the workflows:

| Script | Used by |
|--------|---------|
| `CI/scripts/ci-ubuntu.sh` | ARM cross-arch Docker builds |
| `CI/scripts/build_mingw.sh` | MinGW dependency install + build |
| `CI/scripts/windows_build_deps.cmd` | MSVC dependency download + build |
| `CI/scripts/check_kernel.sh` | Kernel IIO type/modifier sync check |
| `CI/scripts/check_README_BUILD.sh` | CMake option documentation check |
| `CI/scripts/macos_tar_fixup.sh` | macOS tar rpath + dependency bundling |
| `CI/scripts/prepare_assets.sh` | Artifact validation and release preparation |
| `CI/build_win_msvc.ps1` | MSVC CMake build |
| `CI/publish_deps.ps1` | Copy runtime DLL dependencies to artifacts |
| `CI/generate_exe.ps1` | Inno Setup installer generation |
| `CI/run_csharp_tests.ps1` | C# binding smoke + integration tests |

## Secrets

| Secret | Purpose |
|--------|---------|
| `SSH_PRIVATE_KEY` | RSA key for SCP to SW Downloads server |
| `SERVER_ADDRESS` | SCP destination address |
