# https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_preference_variables?view=powershell-7.2#erroractionpreference
$ErrorActionPreference = "Stop"
$ErrorView = "NormalView"

$src_dir=$pwd
$COMPILER=$Env:COMPILER

cd $src_dir
mkdir dependencies
cd dependencies
wget http://swdownloads.analog.com/cse/build/libiio-win-deps-libusb1.0.24.zip -OutFile "libiio-win-deps.zip"
7z x -y "libiio-win-deps.zip"

# Version numbers inside this directory change all the time; print what's
# currently in the folder to make it easier to debug CI breakages on MinGW.
dir C:\ghcup\ghc\

if ($COMPILER -eq "MinGW Makefiles") {
	cp $src_dir\dependencies\libs\64\libserialport-0.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp $src_dir\dependencies\libs\64\libusb-1.0.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\ghcup\ghc\9.2.8\mingw\bin\libgcc_s_seh-1.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\ghcup\ghc\9.6.3\mingw\bin\libiconv-2.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\ghcup\ghc\9.6.3\mingw\bin\zlib1.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\ghcup\ghc\9.6.3\mingw\bin\liblzma-5.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\ghcup\ghc\9.6.3\mingw\bin\libwinpthread-1.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\ghcup\ghc\9.6.3\mingw\bin\libxml2-2.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\ghcup\ghc\9.2.8\mingw\bin\libstdc++-6.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
} else {
	cp $src_dir\dependencies\libs\64\libxml2.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp $src_dir\dependencies\libs\64\libserialport-0.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp $src_dir\dependencies\libs\64\libusb-1.0.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY

	if ($COMPILER -eq "Visual Studio 16 2019") {
		cd 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Redist\MSVC\14.29.30133\x64\Microsoft.VC142.CRT'
		cp .\msvcp140.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
		cp .\vcruntime140.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	} else {
		cd 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC\14.29.30133\x64\Microsoft.VC142.CRT'
		cp .\msvcp140.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
		cp .\vcruntime140.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	}
}
