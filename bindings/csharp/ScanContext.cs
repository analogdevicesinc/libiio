// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2020 Analog Devices, Inc.
 * Author: Cristian Iacob <cristian.iacob@analog.com>
 */

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;

namespace iio
{
    /// <summary> <see cref="iio.ScanContext"/> class:
    /// Class for getting information about the available contexts.</summary>
    public class ScanContext : IDisposable
    {
        private IntPtr scan_block;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_create_scan_block([In] string backend, uint flags);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_scan_block_get_info(IntPtr blk, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_info_get_description(IntPtr info);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_info_get_uri(IntPtr info);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern long iio_scan_block_scan(IntPtr blk);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_scan_block_destroy(IntPtr blk);

        ~ScanContext()
        {
            Dispose(false);
        }

        private void Dispose(bool clean)
        {
            if (this.scan_block != IntPtr.Zero)
            {
                if (clean)
                {
                    GC.SuppressFinalize(this);
                }
                iio_scan_block_destroy(this.scan_block);
                this.scan_block = IntPtr.Zero;
            }
        }

        /// <summary>Releases all resource used by the <see cref="iio.ScanContext"/> object.</summary>
        /// <remarks>Call <see cref="Dispose"/> when you are finished using the <see cref="iio.ScanContext"/>. The
        /// <see cref="Dispose"/> method leaves the <see cref="iio.ScanContext"/> in an unusable state. After calling
        /// <see cref="Dispose"/>, you must release all references to the <see cref="iio.ScanContext"/> so the garbage
        /// collector can reclaim the memory that the <see cref="iio.ScanContext"/> was occupying.</remarks>
        public void Dispose()
        {
            Dispose(true);
        }

        /// <summary>
        /// Gets the uri and the description of all available contexts with USB backend. 
        /// </summary>
        /// <returns>a <see cref="Dictionary{String, String}"/> containing the context's uri as key and its description as value.</returns>
        public Dictionary<string, string> get_usb_backend_contexts()
        {
            this.scan_block = iio_create_scan_block("usb", 0);
            return get_contexts_info();
        }

        // <summary>
        /// Gets the uri and the description of all available contexts with local backend. 
        /// </summary>
        /// <returns>a <see cref="Dictionary{String, String}"/> containing the context's uri as key and its description as value.</returns>
        public Dictionary<string, string> get_local_backend_contexts()
        {
            this.scan_block = iio_create_scan_block("local", 0);
            return get_contexts_info();
        }

        // <summary>
        /// Gets the uri and the description of all available contexts from dns_sd backend. 
        /// </summary>
        /// <returns>a <see cref="Dictionary{String, String}"/> containing the context's uri as key and its description as value.</returns>
        public Dictionary<string, string> get_dns_sd_backend_contexts()
        {
            this.scan_block = iio_create_scan_block("ip", 0);
            return get_contexts_info();
        }

        private Dictionary<string, string> get_contexts_info()
        {
            uint contexts_count = (uint)iio_scan_block_scan(this.scan_block);
            Dictionary<string, string> contexts_info = new Dictionary<string, string>();

            for (uint i = 0; i < contexts_count; i++)
            {
                IntPtr info = iio_scan_block_get_info(this.scan_block, i);
                string uri = Marshal.PtrToStringAnsi(iio_context_info_get_uri(info));
                string description = Marshal.PtrToStringAnsi(iio_context_info_get_description(info));
                contexts_info[uri] = description;
            }

            return contexts_info;
        }
    }
}
