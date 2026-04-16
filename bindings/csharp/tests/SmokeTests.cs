using System;
using System.IO;
using System.Reflection;
using iio;

namespace LibiioTests
{
    class SmokeTests
    {
        static void Main(string[] args)
        {
            Console.WriteLine("========================================");
            Console.WriteLine("libiio C# Bindings - Smoke Tests");
            Console.WriteLine("========================================");

            // Test 1: DLL Existence
            TestFramework.RunTest("DLL Existence Check", TestDllExistence);

            // Test 2: Assembly Loading
            TestFramework.RunTest("Assembly Load Test", TestAssemblyLoad);

            // Test 3: Type Existence
            TestFramework.RunTest("Type Existence Check", TestTypeExistence);

            // Test 4: Backend Information
            TestFramework.RunTest("Backend Info API", TestBackendInfo);

            // Test 5: Backend Availability
            TestFramework.RunTest("Backend Availability Check", TestBackendCheck);

            // Test 6: Error Handling
            TestFramework.RunTest("Exception Handling", TestExceptionHandling);

            // Test 7: Scan Context
            TestFramework.RunTest("Scan Context Creation", TestScanContext);

            // Test 8: Version Info
            TestFramework.RunTest("Library Version Info", TestVersionInfo);

            int exitCode = TestFramework.PrintSummary();

            if (exitCode == 0)
            {
                Console.WriteLine("\n========================================");
                Console.WriteLine("All smoke tests PASSED");
                Console.WriteLine("========================================");
            }
            else
            {
                Console.WriteLine("\n========================================");
                Console.WriteLine("Smoke tests FAILED");
                Console.WriteLine("========================================");
            }

            Environment.Exit(exitCode);
        }

        static void TestDllExistence()
        {
            string[] requiredDlls = new string[]
            {
                "libiio1.dll",
                "libiio-sharp.dll",
                "libusb-1.0.dll",
                "libzstd.dll",
                "libxml2.dll",
                "libserialport.dll"
            };

            foreach (string dll in requiredDlls)
            {
                bool exists = File.Exists(dll);
                TestFramework.AssertTrue(exists, dll + " exists");

                if (exists)
                {
                    FileInfo fi = new FileInfo(dll);
                    TestFramework.AssertTrue(fi.Length > 0,
                        string.Format("{0} has non-zero size ({1} bytes)", dll, fi.Length));
                }
            }
        }

        static void TestAssemblyLoad()
        {
            Assembly assembly = null;
            try
            {
                assembly = Assembly.LoadFrom("libiio-sharp.dll");
                TestFramework.AssertNotNull(assembly, "libiio-sharp.dll loaded successfully");
            }
            catch (Exception ex)
            {
                TestFramework.AssertTrue(false, "Failed to load assembly: " + ex.Message);
            }
        }

        static void TestTypeExistence()
        {
            Type[] expectedTypes = new Type[]
            {
                typeof(Context),
                typeof(Device),
                typeof(Channel),
                typeof(Attr),
                typeof(IOBuffer),
                typeof(Block),
                typeof(iio.Stream),
                typeof(Trigger),
                typeof(Scan),
                typeof(IioLib),
                typeof(IIOException)
            };

            foreach (Type type in expectedTypes)
            {
                TestFramework.AssertNotNull(type, "Type " + type.Name + " exists");
            }
        }

        static void TestBackendInfo()
        {
            try
            {
                int count = IioLib.get_builtin_backends_count();
                TestFramework.AssertTrue(count >= 0,
                    "get_builtin_backends_count returned valid count: " + count);

                if (count > 0)
                {
                    string backend = IioLib.get_builtin_backend(0);
                    TestFramework.AssertNotNull(backend,
                        "get_builtin_backend(0) returned backend: " + backend);
                }
            }
            catch (System.DllNotFoundException ex)
            {
                TestFramework.AssertTrue(false, "DllNotFoundException: " + ex.Message);
            }
            catch (System.EntryPointNotFoundException ex)
            {
                TestFramework.AssertTrue(false, "EntryPointNotFoundException: " + ex.Message);
            }
        }

        static void TestBackendCheck()
        {
            try
            {
                // Test with known backends (should not crash)
                bool hasLocal = IioLib.has_backend("local");
                Console.WriteLine("  INFO: has_backend('local') = " + hasLocal);
                TestFramework.AssertTrue(true, "has_backend executed without crash");

                // Test with invalid backend (should return false, not crash)
                bool hasInvalid = IioLib.has_backend("nonexistent_backend_xyz");
                TestFramework.AssertTrue(!hasInvalid, "has_backend returns false for invalid backend");
            }
            catch (Exception ex)
            {
                TestFramework.AssertTrue(false, "Backend check failed: " + ex.Message);
            }
        }

        static void TestExceptionHandling()
        {
            // Test that attempting to create context with invalid URI throws IIOException
            TestFramework.AssertException<IIOException>(() =>
            {
                Context ctx = new Context("invalid://malformed.uri.12345");
            }, "Invalid context creation throws IIOException");
        }

        static void TestScanContext()
        {
            try
            {
                Scan scan = new Scan();
                TestFramework.AssertNotNull(scan, "Scan object created successfully");

                // nb_results may be 0 if no hardware, but should not crash
                uint resultCount = scan.nb_results;
                Console.WriteLine("  INFO: Scan found " + resultCount + " contexts");
                TestFramework.AssertTrue(true, "Scan completed without crash (found " + resultCount + " results)");

                scan.Dispose();
                TestFramework.AssertTrue(true, "Scan disposed successfully");
            }
            catch (Exception ex)
            {
                TestFramework.AssertTrue(false, "Scan test failed: " + ex.Message);
            }
        }

        static void TestVersionInfo()
        {
            try
            {
                // Get native library version
                iio.Version nativeVersion = IioLib.library_version;
                TestFramework.AssertNotNull(nativeVersion, "IioLib.library_version is not null");
                Console.WriteLine("  INFO: Native library version: " + nativeVersion.major + "." + nativeVersion.minor + " (" + nativeVersion.git_tag + ")");

                // Get C# assembly version
                Assembly assembly = Assembly.LoadFrom("libiio-sharp.dll");
                System.Version assemblyVersion = assembly.GetName().Version;
                Console.WriteLine("  INFO: C# assembly version: " + assemblyVersion.Major + "." + assemblyVersion.Minor + "." + assemblyVersion.Build);

                // Check version compatibility
                // Major version must match - breaking changes
                if (nativeVersion.major != assemblyVersion.Major)
                {
                    TestFramework.AssertTrue(false,
                        "Version MISMATCH: Native major=" + nativeVersion.major +
                        ", C# major=" + assemblyVersion.Major + " (INCOMPATIBLE)");
                }
                else
                {
                    TestFramework.AssertTrue(true, "Major version matches (" + nativeVersion.major + ")");
                }

                // Minor version should match for full compatibility, but warn if different
                if (nativeVersion.minor != assemblyVersion.Minor)
                {
                    Console.WriteLine("  WARN: Minor version mismatch - Native minor=" + nativeVersion.minor +
                                    ", C# minor=" + assemblyVersion.Minor + " (may have limited compatibility)");
                    TestFramework.AssertTrue(true, "Minor version differs (native=" + nativeVersion.minor +
                                                  ", C#=" + assemblyVersion.Minor + ") - proceeding with caution");
                }
                else
                {
                    TestFramework.AssertTrue(true, "Minor version matches (" + nativeVersion.minor + ")");
                }

                TestFramework.AssertTrue(true, "Version compatibility check completed");
            }
            catch (Exception ex)
            {
                TestFramework.AssertTrue(false, "Version check failed: " + ex.Message);
            }
        }
    }
}