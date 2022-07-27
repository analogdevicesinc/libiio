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
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    public class Version
    {
        public readonly uint major;
        public readonly uint minor;
        public readonly string git_tag;

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_context_get_version_major(IntPtr ctx);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_context_get_version_minor(IntPtr ctx);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_version_tag(IntPtr ctx);

        internal Version(uint major, uint minor, string git_tag)
        {
            this.major = major;
            this.minor = minor;
            this.git_tag = git_tag;
        }

        internal Version(IntPtr ctx)
	    : this(iio_context_get_version_major(ctx),
                   iio_context_get_version_minor(ctx),
                   Marshal.PtrToStringAnsi(iio_context_get_version_tag(ctx)))
	{
        }

        internal Version()
	    : this(IntPtr.Zero)
        {
        }
    }

    /// <summary><see cref="iio.Context"/> class:
    /// Contains the representation of an IIO context.</summary>
    public class Context : IIOObject
    {
        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_create_context(IntPtr ctx_params,
            [In()][MarshalAs(UnmanagedType.LPStr)] string uri
        );

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_context_destroy(IntPtr ctx);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_name(IntPtr ctx);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_description(IntPtr ctx);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_xml(IntPtr ctx);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_context_get_devices_count(IntPtr ctx);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_device(IntPtr ctx, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_device_is_trigger(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_context_set_timeout(IntPtr ctx, uint timeout_ms);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_context_clone(IntPtr ctx);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_context_get_attrs_count(IntPtr ctx);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_context_get_attr(IntPtr ctx, uint index, out IntPtr name_ptr, out IntPtr value_ptr);

        /// <summary>A XML representation of the current context.</summary>
        public readonly string xml;

        /// <summary>The name of the current context.</summary>
        public readonly string name;

        /// <summary>Retrieve a human-readable information string about the current context.</summary>
        public readonly string description;
        /// <summary>Retrieve a information about the version context.</summary>
        public readonly Version library_version, backend_version;

        /// <summary>A <c>List</c> of all the IIO devices present on the current context.</summary>
        public readonly List<Device> devices;

        /// <summary>A <c>Dictionary</c> of all the attributes of the current channel. (key, value) = (name, value)</summary>
        public Dictionary<string, string> attrs { get; private set; }

        /// <summary>Initializes a new instance of the <see cref="iio.Context"/> class,
        /// using the provided URI. For compatibility with existing code, providing
        /// an IP address or a hostname here will automatically create a network
        /// context.</summary>
        /// <param name="uri">URI to use for the IIO context creation</param>
        /// <returns>an instance of the <see cref="iio.Context"/> class</returns>
        /// <exception cref="IioLib.IIOException">The IIO context could not be created.</exception>
        public Context(string uri = null) : this(iio_create_context(IntPtr.Zero, uri)) {}

        private Context(IIOPtr ptr)
        {
            if (!ptr)
                throw new IIOException("Unable to create iio.Context", ptr);

            this.hdl = ptr.ptr;

            uint nb_devices = iio_context_get_devices_count(hdl);

            devices = new List<Device>();
            for (uint i = 0; i < nb_devices; i++)
            {
                IntPtr dev = iio_context_get_device(hdl, i);
                if (iio_device_is_trigger(dev))
                {
                    devices.Add(new Trigger(this, dev));
                }
                else
                {
                    devices.Add(new Device(this, dev));
                }
            }

            xml = Marshal.PtrToStringAnsi(iio_context_get_xml(hdl));
            name = Marshal.PtrToStringAnsi(iio_context_get_name(hdl));
            description = Marshal.PtrToStringAnsi(iio_context_get_description(hdl));
            library_version = new Version();
            backend_version = new Version(hdl);

            attrs = new Dictionary<string, string>();
            uint nbAttrs = iio_context_get_attrs_count(hdl);

            for (uint i = 0; i < nbAttrs; i++)
            {
                IntPtr name_ptr;
                IntPtr value_ptr;

                iio_context_get_attr(hdl, i, out name_ptr, out value_ptr);
                string attr_name = Marshal.PtrToStringAnsi(name_ptr);
                string attr_value = Marshal.PtrToStringAnsi(value_ptr);

                attrs[attr_name] = attr_value;
            }
        }

        protected override void Destroy()
        {
            iio_context_destroy(hdl);
        }

        /// <summary>Clone this instance.</summary>
        public Context clone()
        {
            return new Context(iio_context_clone(this.hdl));
        }

        /// <summary>Get the <see cref="iio.Device"/> object of the specified name.</summary>
        /// <param name="name">Name or ID of the device to look for</param>
        /// <exception cref="IioLib.IIOException">The IIO device with the specified
        /// name or ID could not be found in the current context.</exception>
        public Device get_device(string name)
        {
            foreach (Device each in devices) {
                if (each.name.CompareTo(name) == 0 ||
                            each.id.CompareTo(name) == 0)
                {
                    return each;
                }
            }

            throw new IIOException("Device " + name + " not found");
        }

        /// <summary>Set a timeout for I/O operations.</summary>
        /// <param name="timeout">The timeout value, in milliseconds</param>
        /// <exception cref="IioLib.IIOException">The timeout could not be applied.</exception>
        public void set_timeout(uint timeout)
        {
            int ret = iio_context_set_timeout(hdl, timeout);
            if (ret < 0)
                throw new IIOException("Unable to set timeout", ret);
        }
    }
}
