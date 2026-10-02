// Smoke test for the packaged libiio-sharp.dll: fails if it can't load
// the native library or call into it.
using System;
using iio;

try
{
    iio.Version version = IioLib.library_version;
    Console.WriteLine("libiio " + version.major + "." + version.minor);

    int count = IioLib.get_builtin_backends_count();
    if (count == 0)
        throw new Exception("No built-in backends reported");

    for (int i = 0; i < count; i++)
        Console.WriteLine("backend: " + IioLib.get_builtin_backend((uint)i));
}
catch (Exception e)
{
    Console.Error.WriteLine("NuGet smoke test FAILED: " + e);
    Environment.Exit(1);
}
