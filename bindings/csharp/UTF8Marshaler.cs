// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2025 Analog Devices, Inc.
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
        // Rejects invalid byte sequences instead of silently replacing them,
        // so DecodeText can detect a non-UTF-8 buffer and fall back.
        private static readonly Encoding StrictUTF8 = new UTF8Encoding(false, true);

        // Never fails to decode (every byte maps to a codepoint), and agrees
        // with Windows-125x code pages for the Western-European range. Not a
        // perfect substitute for the actual active ANSI code page, but it is
        // dependency-free and recovers the common case correctly.
        private static readonly Encoding Latin1 = Encoding.GetEncoding("ISO-8859-1");

        /// <summary>
        /// Decodes human-readable free text.
        ///
        /// Most backends return UTF-8, but some return text in the
        /// system's active ANSI code page instead, which is not valid UTF-8.
        /// Falling back to Latin-1 recovers that text instead of replacing it
        /// with the Unicode replacement character.
        /// </summary>
        public static string DecodeText(byte[] buffer, int length)
        {
            try
            {
                return StrictUTF8.GetString(buffer, 0, length);
            }
            catch (DecoderFallbackException)
            {
                return Latin1.GetString(buffer, 0, length);
            }
        }

        /// <summary>
        /// Converts a null-terminated UTF-8 (or legacy single-byte) string
        /// pointer to a C# string.
        /// </summary>
        /// <param name="ptr">Pointer to null-terminated string</param>
        /// <returns>Decoded C# string, or null if ptr is IntPtr.Zero</returns>
        public static string PtrToStringUTF8(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero)
                return null;

            int length = 0;
            while (Marshal.ReadByte(ptr, length) != 0)
            {
                length++;
            }

            if (length == 0)
                return string.Empty;

            byte[] buffer = new byte[length];
            Marshal.Copy(ptr, buffer, 0, length);

            return DecodeText(buffer, length);
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

            byte[] bytes = Encoding.UTF8.GetBytes(str);
            IntPtr ptr = Marshal.AllocHGlobal(bytes.Length + 1);

            Marshal.Copy(bytes, 0, ptr, bytes.Length);
            Marshal.WriteByte(ptr, bytes.Length, 0);

            return ptr;
        }
    }
}
