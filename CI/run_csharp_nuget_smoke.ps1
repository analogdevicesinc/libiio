param(
    [string]$StagingDir,
    [string]$Configuration,
    [string]$SourceDir
)

# Set defaults for local execution (non-CI)
if ([string]::IsNullOrEmpty($SourceDir)) {
    if ($env:BUILD_SOURCESDIRECTORY) {
        $SourceDir = $env:BUILD_SOURCESDIRECTORY
    } else {
        $SourceDir = (Get-Location).Path
    }
}

if ([string]::IsNullOrEmpty($StagingDir)) {
    if ($env:BUILD_ARTIFACTSTAGINGDIRECTORY) {
        $StagingDir = $env:BUILD_ARTIFACTSTAGINGDIRECTORY
    } else {
        $StagingDir = "$SourceDir\staging"
    }
}

if ([string]::IsNullOrEmpty($Configuration)) {
    if ($env:cmakeBuildType) {
        $Configuration = $env:cmakeBuildType
    } else {
        $Configuration = "RelWithDebInfo"
    }
}

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "C# NuGet Package Smoke Test (Windows)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Source Directory:  $SourceDir"
Write-Host "  Native DLL Source: $StagingDir\$Configuration"
Write-Host ""

# Packed with a fixed throwaway version: this run only proves libiio1.dll
# and its dependencies load correctly when bundled, not a real release.
$packageVersion = "0.0.0-win-smoke"
$nativeDir = "$StagingDir\$Configuration"
$pkgDir = "$SourceDir\build-msvc\nupkg-smoke"
$smokeDir = "$SourceDir\CI\nuget-smoke"

Write-Host "[1/3] Packing libiio with bundled Windows natives..." -ForegroundColor Yellow
dotnet pack "$SourceDir\bindings\csharp\libiio-sharp-net.csproj" -c Release `
    -p:Version=0.0.0.0 -p:PackageVersion=$packageVersion `
    -p:LibiioSharpTfm=net8.0 `
    -p:LibiioSharpWindowsNativeDir="$nativeDir" `
    -o "$pkgDir"
if ($LASTEXITCODE -ne 0) {
    throw "dotnet pack failed"
}

$nupkg = Get-ChildItem "$pkgDir\*.nupkg" | Select-Object -First 1
Write-Host "  Package: $($nupkg.Name)"
Write-Host ""

Write-Host "[2/3] Verifying runtimes/win-x64/native/libiio1.dll is bundled..." -ForegroundColor Yellow
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($nupkg.FullName)
$entry = $zip.Entries | Where-Object { $_.FullName -eq "runtimes/win-x64/native/libiio1.dll" }
$zip.Dispose()
if (-not $entry) {
    throw "libiio1.dll missing from runtimes/win-x64/native in the package"
}
Write-Host "  OK" -ForegroundColor Green
Write-Host ""

Write-Host "[3/3] Restoring and running, without libiio installed system-wide..." -ForegroundColor Yellow
Remove-Item -Recurse -Force "$smokeDir\obj", "$smokeDir\bin" -ErrorAction SilentlyContinue

Push-Location $smokeDir
try {
    dotnet add package libiio --version $packageVersion --source "$pkgDir"
    if ($LASTEXITCODE -ne 0) {
        throw "dotnet add package failed"
    }

    dotnet build -c Release
    if ($LASTEXITCODE -ne 0) {
        throw "dotnet build failed"
    }

    dotnet run -c Release --no-build
    if ($LASTEXITCODE -ne 0) {
        throw "Smoke test app exited with a non-zero code"
    }
} finally {
    Pop-Location
    # Undo the PackageReference dotnet add package just wrote.
    git -C $SourceDir checkout -- CI/nuget-smoke/NugetSmokeTest.csproj
    Remove-Item -Recurse -Force "$smokeDir\obj", "$smokeDir\bin" -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "SUCCESS: NuGet package smoke test passed" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
