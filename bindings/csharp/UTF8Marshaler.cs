// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace iio
{
    /// <summary>
    /// Helper class for marshaling UTF-8 encoded strings between C and C#.
    /// </summary>
    internal static class UTF8Marshaler
    {
        /// <summary>
        /// Converts a null-terminated UTF-8 string pointer to a C# string.
        /// Handles international characters properly unlike PtrToStringAnsi.
        /// </summary>
        /// <param name="ptr">Pointer to null-terminated UTF-8 string</param>
        /// <returns>C# string decoded from UTF-8, or null if ptr is IntPtr.Zero</returns>
        public static string PtrToStringUTF8(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero)
                return null;

            // For .NET 5.0+ and .NET Core 3.0+, use built-in method
#if NET5_0_OR_GREATER || NETCOREAPP3_0_OR_GREATER
            return Marshal.PtrToStringUTF8(ptr);
#else
            // For older frameworks, manually decode UTF-8
            // Find the null terminator
            int length = 0;
            while (Marshal.ReadByte(ptr, length) != 0)
            {
                length++;
            }

            if (length == 0)
                return string.Empty;

            // Read bytes and decode as UTF-8
            byte[] buffer = new byte[length];
            Marshal.Copy(ptr, buffer, 0, length);

            // Use UTF8 with replacement characters for invalid sequences
            // This prevents crashes on malformed UTF-8
            return Encoding.UTF8.GetString(buffer);
#endif
        }

        /// <summary>
        /// Converts a C# string to a UTF-8 encoded byte array allocated in unmanaged memory.
        /// Caller is responsible for freeing the memory with Marshal.FreeHGlobal.
        /// </summary>
        /// <param name="str">C# string to encode</param>
        /// <returns>Pointer to null-terminated UTF-8 string in unmanaged memory</returns>
        public static IntPtr StringToHGlobalUTF8(string str)
        {
            if (str == null)
                return IntPtr.Zero;

            // For .NET 5.0+ and .NET Core 3.0+, use built-in method
#if NET5_0_OR_GREATER || NETCOREAPP3_0_OR_GREATER
            return Marshal.StringToHGlobalUTF8(str);
#else
            // For older frameworks, manually encode UTF-8
            byte[] bytes = Encoding.UTF8.GetBytes(str);
            IntPtr ptr = Marshal.AllocHGlobal(bytes.Length + 1); // +1 for null terminator

            Marshal.Copy(bytes, 0, ptr, bytes.Length);
            Marshal.WriteByte(ptr, bytes.Length, 0); // Add null terminator

            return ptr;
#endif
        }
    }
}
