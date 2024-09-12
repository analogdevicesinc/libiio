#!/usr/bin/bash.exe
set -xe

init_env() {
    export ARCH=x86_64
    export MINGW_VERSION=mingw64
    export CC=/${MINGW_VERSION}/bin/${ARCH}-w64-mingw32-gcc.exe
    export CXX=/${MINGW_VERSION}/bin/${ARCH}-w64-mingw32-g++.exe
    export CMAKE="/$MINGW_VERSION/bin/cmake"

}

install_pacman_deps() {
    WINDEPS="mingw-w64-x86_64-libserialport \
    mingw-w64-x86_64-libusb \
    mingw-w64-x86_64-zstd \
    mingw-w64-x86_64-libxml2 \
    mingw-w64-x86_64-python3 \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-gcc \
    cmake
    "
    echo "$WINDEPS" | xargs pacman -S --noconfirm --needed
}

build_libiio() {
    $CMAKE -G "MinGW Makefiles" -DPYTHON_EXECUTABLE:FILEPATH=$(python -c "import os, sys; print(os.path.dirname(sys.executable) + '\python.exe')") \
        -DCMAKE_SYSTEM_PREFIX_PATH="C:" -Werror=dev -DCOMPILE_WARNING_AS_ERROR=ON \
        -DENABLE_IPV6=ON -DWITH_USB_BACKEND=ON -DWITH_SERIAL_BACKEND=ON -DPYTHON_BINDINGS=ON -DCPP_BINDINGS=ON \
        -DCSHARP_BINDINGS:BOOL=OFF ..
    $CMAKE --build . --config Release
}

init_env
$@
