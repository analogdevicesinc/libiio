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
    public class IOBuffer : IIOObject
    {
        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_device_create_buffer(IntPtr dev, uint index, IntPtr mask);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_buffer_destroy(IntPtr buf);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_buffer_cancel(IntPtr buf);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_enable(IntPtr buf);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_disable(IntPtr buf);

        internal ChannelsMask mask;

        /// <summary>The size of this buffer, in samples.</summary>
        public readonly uint samples_count;

        /// <summary>The associated <see cref="iio.Device"/> object.</summary>
        public readonly Device dev;

        private bool is_enabled;

        public bool enabled {
            get { return is_enabled; }
            set {
                int err;
                if (value)
                    err = iio_buffer_enable(hdl);
                else
                    err = iio_buffer_disable(hdl);
                if (err != 0)
                    throw new IIOException("Unable to " + (value ? "en" : "dis") + "able buffer", err);
                this.is_enabled = value;
            }
        }

        /// <summary>Initializes a new instance of the <see cref="iio.IOBuffer"/> class.</summary>
        /// <param name="dev">The <see cref="iio.Device"/> object that represents the device
        /// where the I/O operations will be performed.</param>
        /// <param name="mask">The channels mask to use to create the buffer object.</param>
        /// <param name="index">The index of the hardware buffer. Should be 0 in most cases.</param>
        /// <exception cref="IioLib.IIOException">The buffer could not be created.</exception>
        public IOBuffer(Device dev, ChannelsMask mask, uint index = 0)
        {
            this.mask = mask;
            this.dev = dev;

            IIOPtr ptr = iio_device_create_buffer(dev.dev, index, mask.hdl);
            if (!ptr)
                throw new IIOException("Unable to create buffer", ptr);

            this.hdl = ptr.ptr;
            this.is_enabled = false;
        }

        protected override void Destroy()
        {
            iio_buffer_destroy(hdl);
        }

        /// <summary>Cancels the current buffer.</summary>
        public void cancel()
        {
            iio_buffer_cancel(hdl);
        }

        /// <summary>Gets the step size of the current buffer.</summary>
        public uint step()
        {
            return dev.get_sample_size(mask);
        }
    }
}
