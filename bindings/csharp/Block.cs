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
    public class Block : IIOObject
    {
        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_buffer_create_block(IntPtr buf, uint size);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_block_destroy(IntPtr buf);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_block_start(IntPtr buf);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_block_end(IntPtr buf);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_block_first(IntPtr buf, IntPtr chn);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_block_enqueue(IntPtr block, uint bytes_used,
                    [MarshalAs(UnmanagedType.I1)] bool cyclic);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_block_dequeue(IntPtr block,
                    [MarshalAs(UnmanagedType.I1)] bool nonblock);

        private bool enqueued, stream_block;

        public readonly uint size;

        public readonly IOBuffer buf;

        internal Block(IntPtr block, uint size)
        {
            this.buf = null;
            this.size = size;
            this.stream_block = true;
            this.hdl = block;
        }

        public Block(IOBuffer buf, uint size)
        {
            this.buf = buf;
            this.size = size;
            this.enqueued = false;
            this.stream_block = false;

            IIOPtr ptr = iio_buffer_create_block(buf.hdl, size);
            if (!ptr)
                throw new IIOException("Unable to create iio.Block", ptr);

            this.hdl = ptr.ptr;
        }

        protected override void Destroy()
        {
            iio_block_destroy(hdl);
        }

        public int enqueue(uint bytes_used = 0, bool cyclic = false)
        {
            if (enqueued) {
                throw new IIOException("Block is already enqueued");
            }

            if (stream_block) {
                throw new IIOException("Attempt to manually enqueue a iio.Stream block");
            }

            if (bytes_used == 0)
                bytes_used = this.size;

            return iio_block_enqueue(hdl, bytes_used, cyclic);
        }

        public int dequeue(bool nonblock = false)
        {
            if (!enqueued) {
                throw new IIOException("Block has not been enqueued");
            }

            if (stream_block) {
                throw new IIOException("Attempt to manually dequeue a iio.Stream block");
            }

            return iio_block_dequeue(hdl, nonblock);
        }

        /// <summary>Copy the given array of samples inside the <see cref="iio.IOBuffer"/> object.</summary>
        /// <param name="array">A <c>byte</c> array containing the samples that should be written.</param>
        /// <remarks>The number of samples written will not exceed the size of the buffer.</remarks>
        public void fill(byte[] array)
        {
            long length = (long) iio_block_end(hdl) - (long) iio_block_start(hdl);
            if (length > array.Length)
            {
                length = array.Length;
            }
            Marshal.Copy(array, 0, iio_block_start(hdl), (int)length);
        }

        /// <summary>Extract the samples from the <see cref="iio.IOBuffer"/> object.</summary>
        /// <param name="array">A <c>byte</c> array containing the extracted samples.</param>
        public void read(byte[] array)
        {
            long length = (long) iio_block_end(hdl) - (long) iio_block_start(hdl);
            if (length > array.Length)
            {
                length = array.Length;
            }
            Marshal.Copy(iio_block_start(hdl), array, 0, (int)length);
        }

        /// <summary>Gets a pointer to the first sample from the current buffer for a specific channel.</summary>
        /// <param name="ch">The channel for which to find the first sample.</param>
        public IntPtr first(Channel ch)
        {
            if (ch == null)
            {
                throw new IIOException("The channel should not be null!");
            }
            return iio_block_first(hdl, ch.chn);
        }
    }
}
