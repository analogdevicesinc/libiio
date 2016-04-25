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
    public class Version
    {
        public readonly uint major;
        public readonly uint minor;
        public readonly string git_tag;

        internal Version(uint major, uint minor, string git_tag)
        {
            this.major = major;
            this.minor = minor;
            this.git_tag = git_tag;
        }
    }

    /// <summary><see cref="iio.Context"/> class:
    /// Contains the representation of an IIO context.</summary>
    public class Context : IDisposable
    {
        private IntPtr ctx;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_create_network_context(
            [In()][MarshalAs(UnmanagedType.LPStr)] string hostname
        );

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_create_context_from_uri(
            [In()][MarshalAs(UnmanagedType.LPStr)] string uri
        );

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_create_default_context();

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_context_destroy(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_name(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_description(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_xml(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_library_get_version(ref uint major, ref uint minor, [Out()] StringBuilder git_tag);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_context_get_version(IntPtr ctx, ref uint major, ref uint minor, [Out()] StringBuilder git_tag);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_context_get_devices_count(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_device(IntPtr ctx, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_device_is_trigger(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_context_set_timeout(IntPtr ctx, uint timeout_ms);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_clone(IntPtr ctx);

        /// <summary>A XML representation of the current context.</summary>
        public readonly string xml;

        /// <summary>The name of the current context.</summary>
        public readonly string name;

        /// <summary>Retrieve a human-readable information string about the current context.</summary>
        public readonly string description;
        public readonly Version library_version, backend_version;

        /// <summary>A <c>List</c> of all the IIO devices present on the current context.</summary>
        public readonly List<Device> devices;

        /// <summary>Initializes a new instance of the <see cref="iio.Context"/> class,
        /// using the provided URI. For compatibility with existing code, providing
        /// an IP address or a hostname here will automatically create a network
        /// context.</summary>
        /// <param name="uri">URI to use for the IIO context creation</param>
        /// <returns>an instance of the <see cref="iio.Context"/> class</returns>
        /// <exception cref="System.Exception">The IIO context could not be created.</exception>
        public Context(string uri) : this(getContextFromString(uri)) {}

        /// <summary>Initializes a new instance of the <see cref="iio.Context"/> class,
        /// using the local or the network backend of the IIO library.</summary>
        /// <remarks>This function will create a network context if the IIOD_REMOTE
        /// environment variable is set to the hostname where the IIOD server runs.
        /// If set to an empty string, the server will be discovered using ZeroConf.
        /// If the environment variable is not set, a local context will be created
        /// instead.</remarks>
        /// <exception cref="System.Exception">The IIO context could not be created.</exception>
        public Context() : this(iio_create_default_context()) {}

        private static IntPtr getContextFromString(string str)
        {
            IntPtr ptr = iio_create_context_from_uri(str);
            if (ptr == IntPtr.Zero)
                ptr = iio_create_network_context(str);
            return ptr;
        }

        private Context(IntPtr ctx)
        {
            this.ctx = ctx;

            if (ctx == IntPtr.Zero)
                throw new Exception("Unable to create IIO context");

            uint nb_devices = iio_context_get_devices_count(ctx);

            devices = new List<Device>();
            for (uint i = 0; i < nb_devices; i++)
            {
                IntPtr ptr = iio_context_get_device(ctx, i);
                if (iio_device_is_trigger(ptr))
                    devices.Add(new Trigger(this, ptr));
                else
                    devices.Add(new Device(this, ptr));
            }

            xml = Marshal.PtrToStringAnsi(iio_context_get_xml(ctx));
            name = Marshal.PtrToStringAnsi(iio_context_get_name(ctx));
            description = Marshal.PtrToStringAnsi(iio_context_get_description(ctx));

            uint major = 0;
            uint minor = 0;
            StringBuilder builder = new StringBuilder(8);
            iio_library_get_version(ref major, ref minor, builder);
            library_version = new Version(major, minor, builder.ToString());

            major = 0;
            minor = 0;
            builder.Clear();
            int err = iio_context_get_version(ctx, ref major, ref minor, builder);
            if (err < 0)
                throw new Exception("Unable to read backend version");
            backend_version = new Version(major, minor, builder.ToString());
        }

        ~Context()
        {
            if (ctx != IntPtr.Zero)
                Dispose(false);
        }

        /// <summary>Clone this instance.</summary>
        public Context clone()
        {
            return new Context(iio_context_clone(this.ctx));
        }

        /// <summary>Get the <see cref="iio.Device"/> object of the specified name.</summary>
        /// <param name="name">Name or ID of the device to look for</param>
        /// <exception cref="System.Exception">The IIO device with the specified
        /// name or ID could not be found in the current context.</exception>
        public Device get_device(string name)
        {
            foreach (Device each in devices) {
                if (each.name.CompareTo(name) == 0 ||
                            each.id.CompareTo(name) == 0)
                    return each;
            }

            throw new Exception("Device " + name + " not found");
        }

        /// <summary>Set a timeout for I/O operations.</summary>
        /// <param name="timeout">The timeout value, in milliseconds</param>
        /// <exception cref="System.Exception">The timeout could not be applied.</exception>
        public void set_timeout(uint timeout)
        {
            int ret = iio_context_set_timeout(ctx, timeout);
            if (ret < 0)
                throw new Exception("Unable to set timeout");
        }

        /// <summary>Releases all resource used by the <see cref="iio.Context"/> object.</summary>
        /// <remarks>Call <see cref="Dispose"/> when you are finished using the <see cref="iio.Context"/>. The
        /// <see cref="Dispose"/> method leaves the <see cref="iio.Context"/> in an unusable state. After calling
        /// <see cref="Dispose"/>, you must release all references to the <see cref="iio.Context"/> so the garbage
        /// collector can reclaim the memory that the <see cref="iio.Context"/> was occupying.</remarks>
        public void Dispose()
        {
            Dispose(true);
        }

        private void Dispose(bool clean)
        {
            if (ctx != IntPtr.Zero)
            {
                if (clean)
                    GC.SuppressFinalize(this);
                iio_context_destroy(ctx);
                ctx = IntPtr.Zero;
            }
        }
    }
}
