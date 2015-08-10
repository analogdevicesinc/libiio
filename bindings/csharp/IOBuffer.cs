/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * */

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    /// <summary><see cref="iio.IOBuffer"/> class:
    /// The class used for all I/O operations.</summary>
    public class IOBuffer : IDisposable
    {
        private bool circular_buffer_pushed;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_create_buffer(IntPtr dev, uint samples_count,
                                  [MarshalAs(UnmanagedType.I1)] bool circular);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_buffer_destroy(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_refill(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_push_partial(IntPtr buf, uint samples_count);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_buffer_start(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_buffer_end(IntPtr buf);

        internal IntPtr buf;

        /// <summary>The size of this buffer, in samples.</summary>
        public readonly uint samples_count;

        /// <summary>If <c>true</c>, the buffer is circular.</summary>
        public readonly bool circular;

        /// <summary>Initializes a new instance of the <see cref="iio.IOBuffer"/> class.</summary>
        /// <param name="dev">The <see cref="iio.Device"/> object that represents the device
        /// where the I/O operations will be performed.</param>
        /// <param name="samples_count">The size of the buffer, in samples.</param>
        /// <param name="circular">If set to <c>true</c>, the buffer is circular.</param>
        /// <exception cref="System.Exception">The buffer could not be created.</exception>
        public IOBuffer(Device dev, uint samples_count, bool circular = false)
        {
            this.samples_count = samples_count;
            this.circular = circular;
            this.circular_buffer_pushed = false;

            buf = iio_device_create_buffer(dev.dev, samples_count, circular);
            if (buf == IntPtr.Zero)
                throw new Exception("Unable to create buffer");
        }

        ~IOBuffer()
        {
            if (buf != IntPtr.Zero)
                Dispose(false);
        }

        /// <summary>Fetch a new set of samples from the hardware.</summary>
        /// <exception cref="System.Exception">The buffer could not be refilled.</exception>
        public void refill()
        {
            int err = iio_buffer_refill(this.buf);
            if (err < 0)
                throw new Exception("Unable to refill buffer: err=" + err);
        }

        /// <summary>Submit the samples contained in this buffer to the hardware.</summary>
        /// <exception cref="System.Exception">The buffer could not be pushed.</exception>
        public void push(uint samples_count)
        {
            if (circular && circular_buffer_pushed)
                throw new Exception("Circular buffer already pushed\n");

            int err = iio_buffer_push_partial(this.buf, samples_count);
            if (err < 0)
                throw new Exception("Unable to push buffer: err=" + err);
            circular_buffer_pushed = true;
        }

        public void push()
        {
            push(this.samples_count);
        }

        /// <summary>Releases all resource used by the <see cref="iio.IOBuffer"/> object.</summary>
        /// <remarks>Call <see cref="Dispose"/> when you are finished using the <see cref="iio.IOBuffer"/>. The
        /// <see cref="Dispose"/> method leaves the <see cref="iio.IOBuffer"/> in an unusable state. After calling
        /// <see cref="Dispose"/>, you must release all references to the <see cref="iio.IOBuffer"/> so the garbage
        /// collector can reclaim the memory that the <see cref="iio.IOBuffer"/> was occupying.</remarks>
        public void Dispose()
        {
            Dispose(true);
        }

        private void Dispose(bool clean)
        {
            if (buf != IntPtr.Zero)
            {
                if (clean)
                    GC.SuppressFinalize(this);
                iio_buffer_destroy(buf);
                buf = IntPtr.Zero;
            }
        }

        /// <summary>Copy the given array of samples inside the <see cref="iio.IOBuffer"/> object.</summary>
        /// <param name="array">A <c>byte</c> array containing the samples that should be written.</param>
        /// <remarks>The number of samples written will not exceed the size of the buffer.</remarks>
        public void fill(byte[] array)
        {
            int length = (int) iio_buffer_end(buf) - (int) iio_buffer_start(buf);
            if (length > array.Length)
                length = array.Length;
            Marshal.Copy(array, 0, iio_buffer_start(buf), length);
        }
    }
}
