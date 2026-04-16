param(
    [Parameter(Mandatory=$true)]
    [string]$BuildDir,

    [Parameter(Mandatory=$true)]
    [string]$Configuration,

    [Parameter(Mandatory=$true)]
    [string]$TestBinDir,

    [string]$DepsDir = "$BuildDir\..\deps"
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Collecting DLLs for C# Integration Tests" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Build Dir:    $BuildDir"
Write-Host "  Configuration: $Configuration"
Write-Host "  Test Bin Dir: $TestBinDir"
Write-Host "  Deps Dir:     $DepsDir"
Write-Host ""

# Ensure test bin directory exists
if (-not (Test-Path $TestBinDir)) {
    New-Item -ItemType Directory -Path $TestBinDir | Out-Null
}

# Determine Visual Studio version for libusb path
# Check if the environment variable COMPILER is set
$usbVsVersion = "VS2022"
if ($env:COMPILER) {
    if ($env:COMPILER -like "*Visual Studio 16*") {
        $usbVsVersion = "VS2019"
    } elseif ($env:COMPILER -like "*Visual Studio 17*") {
        $usbVsVersion = "VS2022"
    }
}
Write-Host "Using USB library path: $usbVsVersion" -ForegroundColor Yellow

# Define DLL sources and destinations
$dllMappings = @(
    @{
        Name = "libiio1.dll"
        Source = "$BuildDir\$Configuration\libiio1.dll"
        Required = $true
    },
    @{
        Name = "libiio-sharp.dll"
        Source = "$BuildDir\bindings\csharp\libiio-sharp.dll"
        Required = $true
    },
    @{
        Name = "libiio-sharp.pdb"
        Source = "$BuildDir\bindings\csharp\libiio-sharp.pdb"
        Required = $false
    },
    @{
        Name = "libusb-1.0.dll"
        Source = "$DepsDir\libusb\$usbVsVersion\MS64\dll\libusb-1.0.dll"
        Required = $true
    },
    @{
        Name = "libxml2.dll"
        Source = "$DepsDir\libxml2-install\bin\libxml2.dll"
        Required = $true
    },
    @{
        Name = "libzstd.dll"
        Source = "$DepsDir\zstd\build\VS2010\bin\x64_Release\libzstd.dll"
        Required = $true
    },
    @{
        Name = "libserialport.dll"
        Source = "$DepsDir\libserialport\x64\Release\libserialport.dll"
        Required = $true
    }
)

$missingDlls = @()
$copiedDlls = @()

Write-Host "Copying DLLs..." -ForegroundColor Yellow

foreach ($dll in $dllMappings) {
    $dest = Join-Path $TestBinDir $dll.Name

    if (Test-Path $dll.Source) {
        Copy-Item -Path $dll.Source -Destination $dest -Force
        $copiedDlls += $dll.Name

        $fileSize = (Get-Item $dll.Source).Length
        $fileSizeKB = [math]::Round($fileSize / 1KB, 1)
        Write-Host "  [OK] Copied $($dll.Name) ($fileSizeKB KB)" -ForegroundColor Green
    } else {
        if ($dll.Required) {
            $missingDlls += $dll.Name
            Write-Host "  [MISSING] $($dll.Name)" -ForegroundColor Red
            Write-Host "            Expected at: $($dll.Source)" -ForegroundColor Red
        } else {
            Write-Host "  [SKIP] Optional file not found: $($dll.Name)" -ForegroundColor Yellow
        }
    }
}

Write-Host ""

if ($missingDlls.Count -gt 0) {
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "ERROR: Missing Required DLLs" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "The following required DLLs are missing:" -ForegroundColor Red
    foreach ($dll in $missingDlls) {
        Write-Host "  - $dll" -ForegroundColor Red
    }
    Write-Host ""
    Write-Host "Possible causes:" -ForegroundColor Yellow
    Write-Host "  1. Build did not complete successfully" -ForegroundColor Yellow
    Write-Host "  2. Dependencies were not built (check deps/ directory)" -ForegroundColor Yellow
    Write-Host "  3. Wrong build configuration specified" -ForegroundColor Yellow
    Write-Host ""
    exit 1
}

Write-Host "========================================" -ForegroundColor Green
Write-Host "Successfully collected $($copiedDlls.Count) DLLs" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
foreach ($dll in $copiedDlls) {
    Write-Host "  $dll" -ForegroundColor Green
}
Write-Host ""

exit 0
