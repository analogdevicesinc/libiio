using System;
using System.IO;
using iio;

namespace LibiioTests
{
    /// <summary>
    /// Integration tests using the emulation backend
    ///
    /// These tests use the emulation backend to validate the C# API surface.
    /// They mimic the iio_info tool behavior: create context, enumerate devices,
    /// iterate channels, and read attributes.
    ///
    /// The emulation backend requires an XML file describing an IIO device.
    /// Test XML files are located in tests/resources/xmls/ and copied to the test bin directory
    /// during build.
    /// </summary>
    class IntegrationTests
    {
        private const string TEST_XML_FILENAME = "pluto.xml";

        static void Main(string[] args)
        {
            Console.WriteLine("========================================");
            Console.WriteLine("libiio C# Bindings - Integration Tests");
            Console.WriteLine("========================================");

            RunAll();

            int exitCode = TestFramework.PrintSummary();

            if (exitCode == 0)
            {
                Console.WriteLine("\n========================================");
                Console.WriteLine("All integration tests PASSED");
                Console.WriteLine("========================================");
            }
            else
            {
                Console.WriteLine("\n========================================");
                Console.WriteLine("Integration tests FAILED");
                Console.WriteLine("========================================");
            }

            Environment.Exit(exitCode);
        }

        public static void RunAll()
        {
            Console.WriteLine("\n========================================");
            Console.WriteLine("Integration Tests");
            Console.WriteLine("========================================");

            // Check if emulation backend is available
            if (!IioLib.has_backend("emu"))
            {
                Console.WriteLine("[SKIP] Emulation backend not available");
                Console.WriteLine("       Build with -DWITH_EMU_BACKEND=ON to enable");
                return;
            }

            Console.WriteLine("  INFO: Emulation backend is available");

            // Check if test XML file exists
            if (!File.Exists(TEST_XML_FILENAME))
            {
                Console.WriteLine("[SKIP] Test XML file not found: " + TEST_XML_FILENAME);
                return;
            }

            Console.WriteLine("  INFO: Using test XML: " + TEST_XML_FILENAME);

            TestFramework.RunTest("Context Creation with Emulation Backend", TestContextCreation);
            TestFramework.RunTest("Device Enumeration", TestDeviceEnumeration);
            TestFramework.RunTest("Channel Enumeration", TestChannelEnumeration);
            TestFramework.RunTest("Device Attribute Access", TestDeviceAttributeAccess);
            TestFramework.RunTest("Channel Attribute Access", TestChannelAttributeAccess);
        }

        static void TestContextCreation()
        {
            try
            {
                // Create context with emulation backend using our test XML
                string uri = "emu:" + TEST_XML_FILENAME;
                Context ctx = new Context(uri);
                TestFramework.AssertNotNull(ctx, "Context created with emulation backend");

                string description = ctx.description;
                Console.WriteLine("  INFO: Context description: " + description);
                TestFramework.AssertNotNull(description, "Context has description");

                ctx.Dispose();
                TestFramework.AssertTrue(true, "Context disposed successfully");
            }
            catch (Exception ex)
            {
                TestFramework.AssertTrue(false, "Context creation failed: " + ex.Message);
            }
        }

        static void TestDeviceEnumeration()
        {
            try
            {
                string uri = "emu:" + TEST_XML_FILENAME;
                Context ctx = new Context(uri);
                int deviceCount = ctx.devices.Count;
                Console.WriteLine("  INFO: Found " + deviceCount + " devices");
                TestFramework.AssertTrue(deviceCount > 0, "Context has devices (found " + deviceCount + ")");

                for (int i = 0; i < ctx.devices.Count; i++)
                {
                    Device dev = ctx.devices[i];
                    TestFramework.AssertNotNull(dev, "Device " + i + " is accessible");

                    string id = dev.id;
                    string name = dev.name;
                    Console.WriteLine("  INFO: Device " + i + ": id='" + id + "', name='" + name + "'");
                }

                ctx.Dispose();
            }
            catch (Exception ex)
            {
                TestFramework.AssertTrue(false, "Device enumeration failed: " + ex.Message);
            }
        }

        static void TestChannelEnumeration()
        {
            try
            {
                string uri = "emu:" + TEST_XML_FILENAME;
                Context ctx = new Context(uri);

                for (int i = 0; i < ctx.devices.Count; i++)
                {
                    Device dev = ctx.devices[i];
                    int channelCount = dev.channels.Count;
                    Console.WriteLine("  INFO: Device '" + dev.id + "' has " + channelCount + " channels");

                    for (int j = 0; j < dev.channels.Count; j++)
                    {
                        Channel chn = dev.channels[j];
                        TestFramework.AssertNotNull(chn, "Channel " + j + " of device " + i + " is accessible");

                        string id = chn.id;
                        bool isOutput = chn.output;
                        Console.WriteLine("    INFO: Channel " + j + ": id='" + id + "', output=" + isOutput);
                    }
                }

                ctx.Dispose();
            }
            catch (Exception ex)
            {
                TestFramework.AssertTrue(false, "Channel enumeration failed: " + ex.Message);
            }
        }

        static void TestDeviceAttributeAccess()
        {
            try
            {
                string uri = "emu:" + TEST_XML_FILENAME;
                Context ctx = new Context(uri);

                if (ctx.devices.Count == 0)
                {
                    TestFramework.AssertTrue(false, "No devices found in context");
                    return;
                }

                Device dev = ctx.devices[0];
                int attrCount = dev.attrs.Count;
                Console.WriteLine("  INFO: Device has " + attrCount + " attributes");

                foreach (Attr attr in dev.attrs)
                {
                    string name = attr.name;

                    try
                    {
                        string value = attr.read();
                        Console.WriteLine("    INFO: Attribute '" + name + "' = '" + value + "'");
                        TestFramework.AssertTrue(true, "Read attribute '" + name + "'");
                    }
                    catch (Exception)
                    {
                        // Some attributes may not be readable
                        Console.WriteLine("    INFO: Attribute '" + name + "' is not readable");
                    }
                }

                ctx.Dispose();
            }
            catch (Exception ex)
            {
                TestFramework.AssertTrue(false, "Device attribute access failed: " + ex.Message);
            }
        }

        static void TestChannelAttributeAccess()
        {
            try
            {
                string uri = "emu:" + TEST_XML_FILENAME;
                Context ctx = new Context(uri);

                if (ctx.devices.Count == 0)
                {
                    TestFramework.AssertTrue(false, "No devices found in context");
                    return;
                }

                Device dev = ctx.devices[0];

                if (dev.channels.Count > 0)
                {
                    Channel chn = dev.channels[0];
                    int attrCount = chn.attrs.Count;
                    Console.WriteLine("  INFO: Channel '" + chn.id + "' has " + attrCount + " attributes");

                    foreach (Attr attr in chn.attrs)
                    {
                        string name = attr.name;

                        try
                        {
                            string value = attr.read();
                            Console.WriteLine("    INFO: Channel attribute '" + name + "' = '" + value + "'");
                            TestFramework.AssertTrue(true, "Read channel attribute '" + name + "'");
                        }
                        catch (Exception)
                        {
                            // Some attributes may not be readable
                            Console.WriteLine("    INFO: Channel attribute '" + name + "' is not readable");
                        }
                    }
                }
                else
                {
                    Console.WriteLine("  INFO: Device has no channels to test");
                }

                ctx.Dispose();
            }
            catch (Exception ex)
            {
                TestFramework.AssertTrue(false, "Channel attribute access failed: " + ex.Message);
            }
        }
    }
}