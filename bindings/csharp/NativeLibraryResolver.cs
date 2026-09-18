// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

// Mono maps IioLib.dllname to the platform's real library file through the
// dllmap entries of libiio-sharp.dll.config. .NET (Core) ignores dllmap files
// altogether and only probes variations of the name itself, none of which match
// a versioned "libiio.so.1", so the mapping has to be installed at runtime with
// a DllImportResolver instead. Neither NativeLibrary nor module initializers
// exist on Mono / .NET Framework, hence the whole file is compiled out there.
#if NET5_0_OR_GREATER

using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace iio
{
    /// <summary>Resolves <see cref="IioLib.dllname"/> to the native library
    /// file name used by the platform we are running on.</summary>
    internal static class NativeLibraryResolver
    {
        /// <summary>Names tried on macOS. Keep in sync with libiio-sharp.dll.config.</summary>
        private static readonly string[] osx_names = {
            "libiio.1.dylib",
            "libiio.dylib",
            "/Library/Frameworks/iio.framework/iio",
        };

        /// <summary>Names tried on Linux and the other non-Windows platforms.
        /// Keep in sync with <see cref="IioLib.dllname"/> and libiio-sharp.dll.config.</summary>
        private static readonly string[] unix_names = {
            "libiio.so.1",
            "libiio.so",
        };

        // CA2255 discourages module initializers in libraries, but the resolver
        // has to be installed before the first P/Invoke of any type in this
        // assembly, which leaves no other hook the callers cannot bypass.
#pragma warning disable CA2255
        [ModuleInitializer]
#pragma warning restore CA2255
        internal static void Register()
        {
            try
            {
                NativeLibrary.SetDllImportResolver(typeof(IioLib).Assembly, Resolve);
            }
            catch (InvalidOperationException)
            {
                // An application that installed its own resolver before first
                // touching this assembly wins; never throw from here, as that
                // would break every later use of the bindings.
            }
        }

        private static IntPtr Resolve(string libraryName, Assembly assembly,
                                      DllImportSearchPath? searchPath)
        {
            // Windows uses IioLib.dllname as-is, and any other name belongs to
            // someone else; returning zero leaves the default probing alone.
            if (libraryName != IioLib.dllname ||
                RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                return IntPtr.Zero;

            string[] names = RuntimeInformation.IsOSPlatform(OSPlatform.OSX)
                ? osx_names : unix_names;

            foreach (string name in names)
            {
                IntPtr handle;

                if (NativeLibrary.TryLoad(name, assembly, searchPath, out handle))
                    return handle;
            }

            // Let the default resolution fail instead, so that its exception
            // lists every name that has been tried.
            return IntPtr.Zero;
        }
    }
}

#endif
