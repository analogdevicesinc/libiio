param(
    [string]$BuildDir,
    [string]$Configuration,
    [string]$SourceDir
)

# Set defaults for local execution (non-CI)
if ([string]::IsNullOrEmpty($SourceDir)) {
    if ($env:BUILD_SOURCESDIRECTORY) {
        $SourceDir = $env:BUILD_SOURCESDIRECTORY
    } else {
        # Running locally - use current directory as source
        $SourceDir = (Get-Location).Path
    }
}

if ([string]::IsNullOrEmpty($BuildDir)) {
    if ($env:BUILD_SOURCESDIRECTORY) {
        $BuildDir = "$env:BUILD_SOURCESDIRECTORY\build-msvc"
    } else {
        # Running locally - use build subdirectory
        $BuildDir = "$SourceDir\build"
    }
}

if ([string]::IsNullOrEmpty($Configuration)) {
    if ($env:cmakeBuildType) {
        $Configuration = $env:cmakeBuildType
    } else {
        # Default to RelWithDebInfo for local builds
        $Configuration = "RelWithDebInfo"
    }
}

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "C# Bindings Tests" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Build Directory:  $BuildDir"
Write-Host "  Configuration:    $Configuration"
Write-Host "  Source Directory: $SourceDir"
Write-Host ""

# Determine test bin directory
$testBinDir = "$BuildDir\bindings\csharp\tests\csharp-test-bin"

# Step 1: Build the C# tests
Write-Host "[1/3] Building C# tests..." -ForegroundColor Yellow
Write-Host ""

try {
    $buildOutput = cmake --build $BuildDir --config $Configuration --target libiio-csharp-tests 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build output:" -ForegroundColor Red
        Write-Host $buildOutput
        throw "CMake build failed with exit code $LASTEXITCODE"
    }
    Write-Host "  [OK] C# tests built successfully" -ForegroundColor Green
} catch {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "ERROR: Failed to build C# tests" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}

Write-Host ""

# Step 2: Collect DLL dependencies
Write-Host "[2/3] Collecting DLL dependencies..." -ForegroundColor Yellow
Write-Host ""

$depsDir = "$SourceDir\deps"
$dllCollectorScript = "$SourceDir\bindings\csharp\tests\DllCollector.ps1"

try {
    & $dllCollectorScript `
        -BuildDir $BuildDir `
        -Configuration $Configuration `
        -TestBinDir $testBinDir `
        -DepsDir $depsDir

    if ($LASTEXITCODE -ne 0) {
        throw "DLL collection failed"
    }
} catch {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "ERROR: Failed to collect dependencies" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}

# Step 3: Run the tests
Write-Host "[3/3] Running tests..." -ForegroundColor Yellow
Write-Host ""

$smokeTestExe = "$testBinDir\LibiioSmokeTests.exe"
$integrationTestExe = "$testBinDir\LibiioIntegrationTests.exe"

if (-not (Test-Path $smokeTestExe)) {
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "ERROR: Smoke test executable not found" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Expected location: $smokeTestExe" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $integrationTestExe)) {
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "ERROR: Integration test executable not found" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Expected location: $integrationTestExe" -ForegroundColor Red
    exit 1
}

# Change to test directory so DLLs are found
Push-Location $testBinDir

try {
    # Run smoke tests first
    Write-Host "--- Smoke Tests ---" -ForegroundColor Cyan
    & $smokeTestExe
    $smokeExitCode = $LASTEXITCODE

    Write-Host ""

    # Run integration tests
    Write-Host "--- Integration Tests ---" -ForegroundColor Cyan
    & $integrationTestExe
    $integrationExitCode = $LASTEXITCODE

    Write-Host ""

    $testExitCode = if ($smokeExitCode -ne 0 -or $integrationExitCode -ne 0) { 1 } else { 0 }

    if ($testExitCode -eq 0) {
        Write-Host "========================================" -ForegroundColor Green
        Write-Host "SUCCESS: All C# tests PASSED" -ForegroundColor Green
        Write-Host "========================================" -ForegroundColor Green
    } else {
        Write-Host "========================================" -ForegroundColor Red
        Write-Host "FAILURE: C# tests FAILED" -ForegroundColor Red
        Write-Host "========================================" -ForegroundColor Red
        if ($smokeExitCode -ne 0) { Write-Host "  Smoke tests exit code: $smokeExitCode" -ForegroundColor Red }
        if ($integrationExitCode -ne 0) { Write-Host "  Integration tests exit code: $integrationExitCode" -ForegroundColor Red }
    }

    exit $testExitCode
} catch {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "ERROR: Test execution failed" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
} finally {
    Pop-Location
}