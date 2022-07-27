// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

using System;
using System.Runtime.InteropServices;

namespace iio
{
    public class Stream : IIOObject
    {
        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_buffer_create_stream(IntPtr buf,
                    uint nb_blocks, uint samples_count);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_stream_destroy(IntPtr buf);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_stream_get_next_block(IntPtr buf);

        public readonly IOBuffer buf;
        public readonly uint nb_blocks;
        public readonly uint samples_count;

        public Stream(IOBuffer buf, uint nb_blocks, uint samples_count)
        {
            IIOPtr ptr = iio_buffer_create_stream(buf.hdl, nb_blocks, samples_count);
            if (!ptr)
                throw new IIOException("Unable to create iio.Stream", ptr);

            this.hdl = ptr.ptr;
            this.buf = buf;
            this.nb_blocks = nb_blocks;
            this.samples_count = samples_count;
        }

        public Block next()
        {
            IIOPtr ptr = iio_stream_get_next_block(hdl);
            if (!ptr)
                throw new IIOException("Unable to get next block", ptr);

            return new Block(ptr.ptr, buf.step() * samples_count);
        }

        protected override void Destroy()
        {
            iio_stream_destroy(hdl);
        }
    }
}
