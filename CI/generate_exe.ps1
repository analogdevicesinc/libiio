# https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_preference_variables?view=powershell-7.2#erroractionpreference
$ErrorActionPreference = "Stop"
$ErrorView = "NormalView"

SET PATH=packages\Tools.InnoSetup.5.6.1\tools
iscc $env:BUILD_ARTIFACTSTAGINGDIRECTORY\Windows-VS-16-2019-Win32\libiio.iss

Get-ChildItem $env:BUILD_ARTIFACTSTAGINGDIRECTORY -Force -Recurse | Remove-Item -Force -Recurse
cp C:\libiio-setup.exe $env:BUILD_ARTIFACTSTAGINGDIRECTORY
