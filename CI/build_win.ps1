# https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_preference_variables?view=powershell-7.2#erroractionpreference
$ErrorActionPreference = "Stop"
$ErrorView = "NormalView"

$COMPILER=$Env:COMPILER
$USE_CSHARP=$Env:USE_CSHARP
$src_dir=$pwd

echo "Running cmake for $COMPILER on 64 bit..."
mkdir build-x64
cp .\libiio.iss.cmakein .\build-x64
cd build-x64

cmake -G "$COMPILER" -DCMAKE_SYSTEM_PREFIX_PATH="C:" -Werror=dev -DCOMPILE_WARNING_AS_ERROR=ON -DENABLE_IPV6=ON -DWITH_USB_BACKEND=ON -DWITH_SERIAL_BACKEND=ON -DCPP_BINDINGS=ON -DCSHARP_BINDINGS:BOOL=$USE_CSHARP -DLIBXML2_LIBRARIES="C:\\libs\\64\\libxml2.lib" -DLIBUSB_LIBRARIES="C:\\libs\\64\\libusb-1.0.lib" -DLIBSERIALPORT_LIBRARIES="C:\\libs\\64\\libserialport.dll.a" -DLIBUSB_INCLUDE_DIR="C:\\include\\libusb-1.0" -DLIBXML2_INCLUDE_DIR="C:\\include\\libxml2" -DLIBZSTD_INCLUDE_DIR="C:\\include" -DLIBZSTD_LIBRARIES="C:\\libs\\64\\libzstd.dll.a" ..

cmake --build . --config Release
if ( $LASTEXITCODE -ne 0 ) {
		throw "[*] cmake build failure"
	}
cp .\libiio.iss $env:BUILD_ARTIFACTSTAGINGDIRECTORY