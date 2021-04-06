// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.Remoting.Contexts;
using System.Security.Authentication.ExtendedProtection;
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

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_get_poll_fd(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_set_blocking_mode(IntPtr buf, bool blocking);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_buffer_cancel(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_buffer_first(IntPtr buf, IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern long iio_buffer_step(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_context(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_buffer_get_device(IntPtr buf);

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
            {
                throw new Exception("Unable to create buffer");
            }
        }

        ~IOBuffer()
        {
            if (buf != IntPtr.Zero)
            {
                Dispose(false);
            }
        }

        /// <summary>Fetch a new set of samples from the hardware.</summary>
        /// <exception cref="System.Exception">The buffer could not be refilled.</exception>
        public void refill()
        {
            int err = iio_buffer_refill(this.buf);
            if (err < 0)
            {
                throw new Exception("Unable to refill buffer: err=" + err);
            }
        }

        /// <summary>Submit the samples contained in this buffer to the hardware.</summary>
        /// <exception cref="System.Exception">The buffer could not be pushed.</exception>
        public void push(uint samples_count)
        {
            if (circular && circular_buffer_pushed)
            {
                throw new Exception("Circular buffer already pushed\n");
            }

            int err = iio_buffer_push_partial(this.buf, samples_count);
            if (err < 0)
            {
                throw new Exception("Unable to push buffer: err=" + err);
            }
            circular_buffer_pushed = true;
        }

        /// <summary>Submit all the samples contained in this buffer to the hardware.</summary>
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
                {
                    GC.SuppressFinalize(this);
                }
                iio_buffer_destroy(buf);
                buf = IntPtr.Zero;
            }
        }

        /// <summary>Copy the given array of samples inside the <see cref="iio.IOBuffer"/> object.</summary>
        /// <param name="array">A <c>byte</c> array containing the samples that should be written.</param>
        /// <remarks>The number of samples written will not exceed the size of the buffer.</remarks>
        public void fill(byte[] array)
        {
            long length = (long) iio_buffer_end(buf) - (long) iio_buffer_start(buf);
            if (length > array.Length)
            {
                length = array.Length;
            }
            Marshal.Copy(array, 0, iio_buffer_start(buf), (int)length);
        }

        /// <summary>Extract the samples from the <see cref="iio.IOBuffer"/> object.</summary>
        /// <param name="array">A <c>byte</c> array containing the extracted samples.</param>
        public void read(byte[] array)
        {
            long length = (long) iio_buffer_end(buf) - (long) iio_buffer_start(buf);
            if (length > array.Length)
            {
                length = array.Length;
            }
            Marshal.Copy(iio_buffer_start(buf), array, 0, (int)length);
        }

        /// <summary>Returns poll file descriptor for the current buffer.</summary>
        public int get_poll_fd()
        {
            return iio_buffer_get_poll_fd(buf);
        }

        /// <summary>Sets the blocking behavior of the current buffer.</summary>
        /// <param name="blocking">true if blocking buffer, otherwise false</param>
        public int set_blocking_mode(bool blocking)
        {
            return iio_buffer_set_blocking_mode(buf, blocking);
        }

        /// <summary>Cancels the current buffer.</summary>
        public void cancel()
        {
            iio_buffer_cancel(buf);
        }

        /// <summary>Gets the device of the current buffer.</summary>
        /// <returns>The device of the current buffer.</returns>
        public Device get_device()
        {
            IntPtr dev = iio_buffer_get_device(buf);
            return new Device(new Context(iio_device_get_context(dev)), dev);
        }

        /// <summary>Gets a pointer to the first sample from the current buffer for a specific channel.</summary>
        /// <param name="ch">The channel for which to find the first sample.</param>
        public IntPtr first(Channel ch)
        {
            if (ch == null)
            {
                throw new System.Exception("The channel should not be null!");
            }
            return iio_buffer_first(buf, ch.chn);
        }

        /// <summary>Gets the step size of the current buffer.</summary>
        public long step()
        {
            return iio_buffer_step(buf);
        }
    }
}
