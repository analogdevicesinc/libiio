# https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_preference_variables?view=powershell-7.2#erroractionpreference
$ErrorActionPreference = "Stop"
$ErrorView = "NormalView"

# Discover MSVC redistributable version
$vcRedistPath = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC\"

# Get the VC143 version (skip v143 folder, get the numbered version)
$vcVersion = (Get-ChildItem $vcRedistPath -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object Name -Descending | Select-Object -First 1).Name

if (-not $vcVersion) {
    Write-Error "Could not find MSVC redistributable version in $vcRedistPath"
    exit 1
}

Write-Output "Discovered MSVC Redistributable Version: $vcVersion"
Write-Output "Full paths to runtime DLLs:"
Write-Output "  msvcp140.dll: ${vcRedistPath}${vcVersion}\x64\Microsoft.VC143.CRT\msvcp140.dll"
Write-Output "  vcruntime140.dll: ${vcRedistPath}${vcVersion}\x64\Microsoft.VC143.CRT\vcruntime140.dll"

# Replace placeholder with actual version in the .iss file
$issFile = "$env:BUILD_ARTIFACTSTAGINGDIRECTORY\Windows-VS-2022-x64\libiio.iss"
(Get-Content $issFile) -replace 'VCREDIST_VERSION', $vcVersion | Set-Content $issFile

iscc $issFile

Get-ChildItem $env:BUILD_ARTIFACTSTAGINGDIRECTORY -Force -Recurse | Remove-Item -Force -Recurse
cp C:\libiio-setup.exe $env:BUILD_ARTIFACTSTAGINGDIRECTORY
