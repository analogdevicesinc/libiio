// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2020 Analog Devices, Inc.
 * Author: Paul Cristian Iacob <cristian.iacob@analog.com>
 */

using System;
using System.Linq;
using System.Runtime.InteropServices;

namespace iio
{
    /// <summary><see cref="iio.IioLib"/> class:
    /// Contains the general methods from libiio.</summary>
    public static class IioLib
    {
        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_strerror(int err, [In] string buf, ulong len);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_has_backend([In] string backend);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_get_backends_count();

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_get_backend(uint index);

        /// <summary>Calls the iio_strerror method from libiio.</summary>
        /// <param name="err">Error code.</param>
        /// <param name="buf">Error message.</param>
        public static void strerror(int err, string buf)
        {
            if (buf == null)
            {
                throw new System.Exception("The buffer should not be null!");
            }
            iio_strerror(err, buf, (ulong) buf.Length);
        }

        /// <summary>Checks if the given backend is available or not.</summary>
        /// <param name="backend">The backend's name.</param>
        public static bool has_backend(string backend)
        {
            if (backend == null)
            {
                throw new System.Exception("The backend string should not be null!");
            }
            return iio_has_backend(backend);
        }

        /// <summary>Gets the total number of available backends.</summary>
        public static int get_backends_count()
        {
            return iio_get_backends_count();
        }

        /// <summary>Gets the backend from the given index.</summary>
        public static string get_backend(uint index)
        {
            return Marshal.PtrToStringAnsi(iio_get_backend(index));
        }
    }
}
