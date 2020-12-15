
$COMPILER=$Env:COMPILER
$ARCH=$Env:ARCH

$src_dir=$pwd

mkdir build
cd build

cmake -G "$COMPILER" -A "$ARCH" -DPYTHON_BINDINGS=ON -DLIBXML2_LIBRARIES="$src_dir\deps\lib\libxml2.dll.a" ..
cmake --build . --config Release
