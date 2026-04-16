# C# Bindings Integration Tests

This directory contains integration tests for the libiio C# bindings (`libiio-sharp.dll`). These tests validate that the C# bindings correctly interact with the native libiio library and catch common deployment issues before they reach end users.

## Files in This Directory

| File | Purpose |
|------|---------|
| **CMakeLists.txt** | CMake build configuration that compiles the C# test executables using the C# compiler (csc.exe on Windows, mcs on Unix). Automatically builds `LibiioSmokeTests.exe` and `LibiioIntegrationTests.exe` (combined under the `libiio-csharp-tests` target) when C# bindings are enabled. |
| **TestFramework.cs** | Lightweight test framework providing assertion methods, test execution, and result reporting. Contains `AssertTrue()`, `AssertNotNull()`, `AssertEqual()`, `AssertException()`, and summary reporting. No external dependencies required. |
| **SmokeTests.cs** | Smoke tests that validate basic functionality without requiring hardware. Contains the `Main()` entry point. Tests DLL existence, assembly loading, P/Invoke resolution, backend checks, exception handling, and scan functionality. |
| **IntegrationTests.cs** | Integration tests designed for use with the emulation backend. Mimics `iio_info` tool behavior: create context, enumerate devices/channels, read attributes. Uses XML files from `../../tests/resources/xmls/` to define emulated devices. |
| **DllCollector.ps1** | PowerShell script that collects all required DLL dependencies from the build and deps directories into the test bin directory. Handles VS version differences and provides clear error messages for missing dependencies. |

## Test Resources

XML test files for the emulation backend are located in `tests/resources/xmls/` (repository root). These files define IIO device configurations used by integration tests. During build, CMake automatically copies these XML files to the test bin directory so they can be referenced by filename in the test code.

To add new test devices, place XML files in `tests/resources/xmls/` and reference them by filename in `IntegrationTests.cs`.

## Test types

### Smoke Tests

**Purpose**: Validate that the C# bindings can be deployed and used without hardware.

**What it tests**:
1. **DLL Existence**: Verifies all 6 required DLLs are present and non-empty
   - `libiio1.dll` (native library)
   - `libiio-sharp.dll` (C# assembly)
   - `libusb-1.0.dll` (USB backend)
   - `libzstd.dll` (compression)
   - `libxml2.dll` (XML support)
   - `libserialport.dll` (serial backend)

2. **Assembly Loading**: Confirms `libiio-sharp.dll` can be loaded via reflection

3. **Type Existence**: Validates all expected C# types are present in the assembly
   - Context, Device, Channel, Attr, IOBuffer, Block, Stream, Trigger, Scan, IioLib, IIOException

4. **P/Invoke Resolution**: Tests that native function calls work
   - `IioLib.get_builtin_backends_count()` - Basic P/Invoke
   - `IioLib.get_builtin_backend()` - String marshaling
   - `IioLib.has_backend()` - Boolean return values

5. **Backend Availability**: Checks backend availability without crashing
   - Tests `has_backend()` with known and invalid backend names
   - Verifies boolean return values work correctly

6. **Exception Handling**: Verifies error handling works correctly
   - Invalid context creation throws `IIOException` (not crash)

7. **Scan Functionality**: Tests context discovery without crashing
   - Creates `Scan` object
   - Reads `nb_results` property (may be 0 without hardware)
   - Disposes properly

8. **Version Compatibility**: Validates native library and C# assembly version compatibility
   - Reads native library version from `libiio1.dll` via `IioLib.library_version`
   - Reads C# assembly version from `libiio-sharp.dll` using reflection
   - **FAILS if major versions differ** (incompatible, breaking API changes)
   - **WARNS if minor versions differ** (may have limited compatibility)
   - **PASSES when versions match** (fully compatible)
   - Critical for users deploying different versions of `libiio.dll` with the C# bindings

**Exit Code**:
- `0` = All tests passed
- `1` = One or more tests failed

### Integration Tests

**Purpose**: Exercise the complete C# API surface using emulated hardware.

**What it tests**:
1. **Context Creation**: Create context with emulation backend using XML device definitions
2. **Device Enumeration**: Iterate all emulated devices
3. **Channel Enumeration**: Iterate channels for each device
4. **Device Attribute Access**: Read device attributes
5. **Channel Attribute Access**: Read channel attributes

These tests mimic the behavior of the `iio_info` utility tool but in C#, ensuring the bindings support complete workflows. The tests use XML files from `tests/resources/xmls/` (e.g., `pluto.xml`) to define the emulated IIO devices. The emulation backend allows testing the API without physical hardware.

## What These Tests Are For

### Problem Statement

Users frequently integrate `libiio-sharp.dll` into their C# applications and encounter deployment issues:

- **Missing DLLs**: Native dependencies not included in deployment packages
- **Architecture Mismatches**: Mixing x86/x64 DLLs
- **API Breaking Changes**: C API changes that the C# bindings don't match
- **P/Invoke Failures**: DllNotFoundException or EntryPointNotFoundException at runtime
- **Version Incompatibilities**: Mismatched libiio.dll and libiio-sharp.dll versions

These issues are often discovered by end users in production rather than during development.

### Solution

These integration tests catch deployment and compatibility issues early:

1. **CI/CD Integration**: Tests run automatically on every Windows build in Azure Pipelines
2. **Fail Fast**: Build fails if C# bindings cannot be used, preventing broken releases
3. **Clear Diagnostics**: Detailed error messages identify exactly which DLL is missing or which API call failed
4. **No Hardware Required**: Smoke tests validate deployment without needing physical IIO devices
5. **Version Validation**: Ensures C# bindings match the native library version they were built against

### Use Cases

**For Developers**:
- Verify C# bindings work after modifying the C API
- Catch breaking changes before they reach users
- Test deployment packaging locally before release

**For CI/CD**:
- Automated validation on every build
- Prevents publishing broken C# bindings
- Validates all required dependencies are built and available

**For End Users**:
- Confidence that released C# bindings are functional
- Example code showing correct usage patterns
- Reference for required DLL dependencies

## Running the Tests

### Local Execution

From the repository root:

```powershell
# Default (uses build/ directory, RelWithDebInfo configuration)
.\CI\run_csharp_tests.ps1

# Specify custom build directory or configuration
.\CI\run_csharp_tests.ps1 -BuildDir "C:\path\to\build" -Configuration "Debug"
```

### Azure Pipelines

Tests run automatically on the `WindowsBuilds` job (VS 2022 x64 configuration only) after the build completes. The pipeline will fail if tests fail.

### Manual Execution

If you want to run the tests manually:

```powershell
# 1. Build the test executable
cmake --build . --config RelWithDebInfo --target libiio-csharp-tests

# 2. Collect DLL dependencies
.\bindings\csharp\tests\DllCollector.ps1 -BuildDir ".\build" -Configuration "RelWithDebInfo" -TestBinDir ".\build\bindings\csharp\tests\csharp-test-bin"

# 3. Run the tests
cd build\bindings\csharp\tests\csharp-test-bin
.\LibiioSmokeTests.exe
.\LibiioIntegrationTests.exe
```

## Expected Output

```
========================================
libiio C# Bindings - Smoke Tests
========================================

[TEST] DLL Existence Check
  [PASS] libiio1.dll exists
  [PASS] libiio1.dll has non-zero size (192512 bytes)
  [PASS] libiio-sharp.dll exists
  ...

[TEST] Backend Info API
  [PASS] get_builtin_backends_count returned valid count: 4
  [PASS] get_builtin_backend(0) returned backend: local
  ...

========================================
TEST SUMMARY
========================================
Total:  22
Passed: 22
Failed: 0
Success Rate: 100.0%

========================================
All smoke tests PASSED
========================================
```

## Extending the Tests

### Adding New Smoke Tests

1. Open `SmokeTests.cs`
2. Add a new test method following the existing pattern
3. Call `TestFramework.RunTest("Test Name", TestMethod)` in `Main()`

Example:
```csharp
static void TestNewFeature()
{
    try
    {
        // Your test code here
        TestFramework.AssertTrue(condition, "Description of what passed");
    }
    catch (Exception ex)
    {
        TestFramework.AssertTrue(false, "Test failed: " + ex.Message);
    }
}

// In Main():
TestFramework.RunTest("New Feature Test", TestNewFeature);
```
