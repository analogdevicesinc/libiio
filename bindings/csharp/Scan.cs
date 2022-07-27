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
    public class Scan : IIOObject
    {
        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_scan(IntPtr ctx_params, [In] string backend);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_scan_destroy(IntPtr scan);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_scan_get_results_count(IntPtr scan);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_scan_get_description(IntPtr ctx, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_scan_get_uri(IntPtr ctx, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_scan_block_get_info(IntPtr blk, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_info_get_description(IntPtr info);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_info_get_uri(IntPtr info);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern long iio_scan_block_scan(IntPtr blk);

        /// <summary>The number of contexts scanned.</summary>
        public readonly uint nb_results;

        /// <summary>A <see cref="Dictionary{String, String}"/> containing each context's uri as key and its description as value.</summary>
        public readonly Dictionary<string, string> results;

        public Scan(string backends = null)
        {
            IIOPtr ptr = iio_scan(IntPtr.Zero, backends);
            if (!ptr)
                throw new IIOException("Unable to create iio.Scan", ptr);

            hdl = ptr.ptr;
            nb_results = iio_scan_get_results_count(hdl);

            results = new Dictionary<string, string>();

            for (uint i = 0; i < nb_results; i++) {
                string uri = Marshal.PtrToStringAnsi(iio_scan_get_uri(hdl, i));
                string dsc = Marshal.PtrToStringAnsi(iio_scan_get_description(hdl, i));

                results[uri] = dsc;
            }
        }

        protected override void Destroy()
        {
            iio_scan_destroy(hdl);
        }
    }
}
