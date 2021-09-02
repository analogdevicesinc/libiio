
$COMPILER=$Env:COMPILER
$ARCH=$Env:ARCH

$src_dir=$pwd

mkdir build
cd build

cmake -G "$COMPILER" -A "$ARCH" -DENABLE_IPV6=OFF -DWITH_USB_BACKEND=OFF -DWITH_SERIAL_BACKEND=OFF -DPYTHON_BINDINGS=ON -DLIBXML2_LIBRARIES="$src_dir\deps\lib\libxml2.dll.a" ..
cmake --build . --config Release

iscc "$src_dir\libiio.iss"
