
$ARCH=$Env:ARCH

git submodule update --init

if (!(Test-Path deps)) {
	mkdir deps
}
cd deps

mkdir libxml

if ( "$ARCH" -eq "x64" ) {
	wget https://www.zlatkovic.com/pub/libxml/64bit/libxml2-2.9.3-win32-x86_64.7z -OutFile "libxml.7z"
} else {
	wget https://www.zlatkovic.com/pub/libxml/64bit/libxml2-2.9.3-win32-x86.7z -OutFile "libxml.7z"
}
7z x -y libxml.7z
rm libxml.7z

echo "Downloading deps..."
cd C:\
wget http://swdownloads.analog.com/cse/build/libiio-win-deps-libusb1.0.24.zip -OutFile "libiio-win-deps.zip"
7z x -y "C:\libiio-win-deps.zip"

# Note: InnoSetup is already installed on Azure images; so don't run this step
#       Running choco seems a bit slow; seems to save about 40-60 seconds here
#choco install InnoSetup

set PATH=%PATH%;"C:\Program Files (x86)\Inno Setup 6"
