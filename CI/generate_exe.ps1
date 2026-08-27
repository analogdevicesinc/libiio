# https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_preference_variables?view=powershell-7.2#erroractionpreference
$ErrorActionPreference = "Stop"
$ErrorView = "NormalView"

# Fix paths in the .iss file: the template has hardcoded Azure Pipelines paths
# (D:\a\1\s for sources, D:\a\1\a for artifacts) which differ on GitHub Actions.
$issFile = "$env:BUILD_ARTIFACTSTAGINGDIRECTORY\Windows-VS-2022-x64\libiio.iss"
(Get-Content $issFile) `
    -replace [regex]::Escape('D:\a\1\s'), $env:BUILD_SOURCESDIRECTORY `
    -replace [regex]::Escape('D:\a\1\a'), $env:BUILD_ARTIFACTSTAGINGDIRECTORY |
    Set-Content $issFile

iscc $issFile

Get-ChildItem $env:BUILD_ARTIFACTSTAGINGDIRECTORY -Force -Recurse | Remove-Item -Force -Recurse
cp C:\libiio-setup.exe $env:BUILD_ARTIFACTSTAGINGDIRECTORY
