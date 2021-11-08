SET PATH=packages\Tools.InnoSetup.5.6.1\tools
iscc $env:BUILD_ARTIFACTSTAGINGDIRECTORY\Windows-VS-16-2019-Win32\libiio.iss

Get-ChildItem $env:BUILD_ARTIFACTSTAGINGDIRECTORY -Force -Recurse | Remove-Item -Force -Recurse
cp C:\libiio-setup.exe $env:BUILD_ARTIFACTSTAGINGDIRECTORY
