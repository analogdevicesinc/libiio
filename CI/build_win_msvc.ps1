# https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_preference_variables?view=powershell-7.2#erroractionpreference
$ErrorActionPreference = "Stop"
$ErrorView = "NormalView"

echo "Running cmake for $Env:COMPILER on 64 bit..."
mkdir build-msvc
cp .\libiio.iss.cmakein .\build-msvc
cd build-msvc
if ( "$Env:COMPILER" -eq "Visual Studio 17 2022" ){
	$VS_version="VS2022"
}elseif ( "$Env:COMPILER" -eq "Visual Studio 16 2019" ) {
	$VS_version="VS2019"
}
cmake -G "$Env:COMPILER" -DPYTHON_EXECUTABLE:FILEPATH=$(python -c "import os, sys; print(os.path.dirname(sys.executable) + '\python.exe')") -DCMAKE_SYSTEM_PREFIX_PATH="C:" `
-Werror=dev -DCOMPILE_WARNING_AS_ERROR=ON -DENABLE_IPV6=ON -DWITH_USB_BACKEND=ON -DWITH_SERIAL_BACKEND=ON -DPYTHON_BINDINGS=ON -DCPP_BINDINGS=ON -DCSHARP_BINDINGS:BOOL=ON `
-DLIBXML2_LIBRARIES="$Env:BUILD_SOURCESDIRECTORY\deps\libxml2-install\lib\libxml2.lib" -DLIBXML2_INCLUDE_DIR="$Env:BUILD_SOURCESDIRECTORY\deps\libxml2-install\include\libxml2" `
-DLIBUSB_LIBRARIES="$Env:BUILD_SOURCESDIRECTORY\deps\libusb\$VS_version\MS64\dll\libusb-1.0.lib" -DLIBUSB_INCLUDE_DIR="$Env:BUILD_SOURCESDIRECTORY\deps\libusb\include" `
-DLIBSERIALPORT_LIBRARIES="$Env:BUILD_SOURCESDIRECTORY\deps\libserialport\x64\Release\libserialport.lib" -DLIBSERIALPORT_INCLUDE_DIR="$Env:BUILD_SOURCESDIRECTORY\deps\libserialport" `
-DLIBZSTD_LIBRARIES="$Env:BUILD_SOURCESDIRECTORY\deps\zstd\build\VS2010\bin\x64_Release\libzstd.lib" -DLIBZSTD_INCLUDE_DIR="$Env:BUILD_SOURCESDIRECTORY\deps\zstd\lib" ..

cmake --build . --config Release

if ( $LASTEXITCODE -ne 0 ) {
		throw "[*] cmake build failure"
	}
cp .\libiio.iss $env:BUILD_ARTIFACTSTAGINGDIRECTORY

cd bindings/python
python.exe setup.py sdist
Get-ChildItem dist\pylibiio-*.tar.gz | Rename-Item -NewName "libiio-py39-amd64.tar.gz"
mv .\dist\*.gz .
rm .\dist\*.gz
