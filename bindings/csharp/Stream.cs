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
        private static extern IIOPtr iio_buffer_create_stream_new(IntPtr buf,
                    uint nb_blocks, uint samples_count, IntPtr mask);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_stream_destroy(IntPtr stream);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_stream_get_next_block(IntPtr stream);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_stream_cancel(IntPtr stream);

        public readonly IOBuffer buf;
        public readonly ChannelsMask mask;
        public readonly uint nb_blocks;
        public readonly uint samples_count;
        private uint sample_size;

        public Stream(IOBuffer buf, ChannelsMask mask, uint nb_blocks, uint samples_count)
        {
            if (mask == null)
                throw new IIOException("A channels mask is required to create a stream");

            IIOPtr ptr = iio_buffer_create_stream_new(buf.buf, nb_blocks, samples_count, mask.hdl);
            if (!ptr)
                throw new IIOException("Unable to create iio.Stream", ptr);

            this.hdl = ptr.ptr;
            this.buf = buf;
            this.mask = mask;
            this.nb_blocks = nb_blocks;
            this.samples_count = samples_count;
            this.sample_size = buf.dev.get_sample_size(mask);
        }

        public void cancel()
        {
            iio_stream_cancel(hdl);
        }

        public Block next()
        {
            IIOPtr ptr = iio_stream_get_next_block(hdl);
            if (!ptr)
                throw new IIOException("Unable to get next block", ptr);

            return new Block(ptr.ptr, this.sample_size);
        }

        protected override void Destroy()
        {
            iio_stream_destroy(hdl);
        }
    }
}
