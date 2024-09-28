:: this script will download and build the necessary dependencies of libiio for windows operating systems
:: if is run from the root of the libiio project all the dependencies will be found in the deps folder of the project
:: in case you already have some dependencies build just comment them in the script and during compilation of libiio specify the path
:: export as environment variables the ARCH, PLATFORM_TOOLSET and COMPILER used for visual studio build
:: ex: set ARCH=x64, PLATFROM_TOOLSET=v143 and COMPILER='Visual Studio 17 2022' for VS2022

IF not exist .\deps (
    echo Directory does not exist. Creating it...
    mkdir .\deps
) ELSE (
    echo Directory already exists.
    cd .\deps
)
choco install -y wget

:: set the msbuild compiler in order to build from terminal the visual studio projects
SETLOCAL ENABLEDELAYEDEXPANSION
IF "%COMPILER%" == "Visual Studio 16 2019" SET vswhere_params=-version [16,17) -products *
IF "%COMPILER%" == "Visual Studio 17 2022" SET vswhere_params=-version [17,18) -products *
SET vswhere="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
FOR /F "USEBACKQ TOKENS=*" %%F IN (`%vswhere% !vswhere_params! -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) DO (
SET msbuild="%%F"
)
:: clone the zstd repo and build it with the desierd configuration
git clone --branch v1.5.6 https://github.com/facebook/zstd.git
%msbuild% .\zstd\build\VS2010\zstd.sln /p:Platform=%ARCH% /p:Configuration=Release /p:PlatformToolset=%PLATFORM_TOOLSET%

:: download libusb release
wget https://github.com/libusb/libusb/releases/download/v1.0.27/libusb-1.0.27.7z
7z x -y .\libusb-1.0.27.7z -o".\libusb"

:: download and build libxml2 library with cmake
wget https://download.gnome.org/sources/libxml2/2.9/libxml2-2.9.14.tar.xz
cmake -E tar xf .\libxml2-2.9.14.tar.xz
cmake -DCMAKE_INSTALL_PREFIX=libxml2-install -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_LZMA=OFF -DLIBXML2_WITH_PYTHON=OFF -DLIBXML2_WITH_ZLIB=OFF -S .\libxml2-2.9.14\ -B libxml2-build
cmake --build libxml2-build --config Release --target install

:: clone and build libserialport project
git clone https://github.com/sigrokproject/libserialport.git
%msbuild% .\libserialport\libserialport.vcxproj /p:Platform=%ARCH% /p:Configuration=Release /p:PlatformToolset=%PLATFORM_TOOLSET%
