# https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_preference_variables?view=powershell-7.2#erroractionpreference
$ErrorActionPreference = "Stop"
$ErrorView = "NormalView"

if ("$Env:COMPILER" -eq "MinGW Makefiles") {
	cp C:\msys64\mingw64\bin\libserialport-0.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\msys64\mingw64\bin\libusb-1.0.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\msys64\mingw64\bin\libxml2-16.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\msys64\mingw64\bin\libzstd.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\msys64\mingw64\bin\libgcc_s_seh-1.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\msys64\mingw64\bin\libiconv-2.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\msys64\mingw64\bin\zlib1.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\msys64\mingw64\bin\liblzma-5.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\msys64\mingw64\bin\libwinpthread-1.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
	cp C:\msys64\mingw64\bin\libstdc++-6.dll $env:BUILD_ARTIFACTSTAGINGDIRECTORY
} else {
	if ( "$Env:COMPILER" -eq "Visual Studio 17 2022" ){
		$USB_VS_version="VS2022"
		cd 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC\14.29.30133\x64\Microsoft.VC142.CRT'
	}elseif ( "$Env:COMPILER" -eq "Visual Studio 16 2019" ) {
		$USB_VS_version="VS2019"
		cd 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Redist\MSVC\14.29.30133\x64\Microsoft.VC142.CRT'
	}elseif ( "$Env:COMPILER" -eq "Visual Studio 18 2026" ) {
		$USB_VS_version="VS2022"
		cd 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Redist\MSVC\14.29.30133\x64\Microsoft.VC142.CRT'
	}
	$targetDirs = @("$env:BUILD_ARTIFACTSTAGINGDIRECTORY\$Env:cmakeBuildType")
	if ($Env:isRelease -eq "false") {
		$targetDirs += "$env:BUILD_ARTIFACTSTAGINGDIRECTORY\Debug"
	}

	foreach ($dir in $targetDirs) {
		cp .\msvcp140.dll $dir
		cp .\vcruntime140.dll $dir
		cp $Env:BUILD_SOURCESDIRECTORY\deps\libxml2-install\bin\libxml2.dll $dir
		cp $Env:BUILD_SOURCESDIRECTORY\deps\libusb\$USB_VS_version\MS64\dll\libusb-1.0.dll $dir
		cp $Env:BUILD_SOURCESDIRECTORY\deps\libserialport\x64\Release\libserialport.dll $dir
		cp $Env:BUILD_SOURCESDIRECTORY\deps\zstd\build\VS2010\bin\x64_Release\libzstd.dll $dir
	}
}
