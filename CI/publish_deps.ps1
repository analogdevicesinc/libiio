
$src_dir=$pwd

cd 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Redist\MSVC\14.29.30133'
if ($ARCH -eq "Win32") {
	echo "$PWD"
	mv .\x86\Microsoft.VC142.CRT\msvcp140.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
}else {
	echo "$PWD"
        mv .\x64\Microsoft.VC142.CRT\msvcp140.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
}

cd $src_dir
mkdir dependencies
cd dependencies
wget http://swdownloads.analog.com/cse/build/libiio-win-deps-libusb1.0.24.zip -OutFile "libiio-win-deps.zip"
7z x -y "libiio-win-deps.zip"

if ($ARCH -eq "Win32") {
	mv .\libs\32\libxml2.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	mv .\libs\32\libserialport-0.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	mv .\libs\32\libusb-1.0.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
}else {
	mv .\libs\64\libxml2.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
        mv .\libs\64\libserialport-0.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
        mv .\libs\64\libusb-1.0.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
}
