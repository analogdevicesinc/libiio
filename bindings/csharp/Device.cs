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
    /// <summary><see cref="iio.Device"/> class:
    /// Contains the representation of an IIO device.</summary>
    public class Device
    {
        /// <summary>Gets the context of the current device.</summary>
        public readonly Context ctx;

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_id(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_name(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_label(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_device_get_channels_count(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_channel(IntPtr dev, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_device_get_attrs_count(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_device_get_debug_attrs_count(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_attr(IntPtr dev, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_debug_attr(IntPtr dev, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_device_get_trigger(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_set_trigger(IntPtr dev, IntPtr trigger);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_get_sample_size(IntPtr dev, IntPtr mask);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_reg_write(IntPtr dev, uint addr, uint value);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_reg_read(IntPtr dev, uint addr, ref uint value);

        internal IntPtr dev;

        /// <summary>An identifier of this device.</summary>
        /// <remarks>The identifier is only valid in this IIO context</remarks>
        public readonly string id;

        /// <summary>The name of this device.</summary>
        public readonly string name;

        /// <summary>The label of this device.</summary>
        public string label { get; private set; }

        /// <summary>True if the device is a hardware monitoring device, False if it is a IIO device.</summary>
        public bool hwmon { get; private set; }

        /// <summary>A <c>list</c> of all the attributes that this device has.</summary>
        public readonly List<Attr> attrs;

        /// <summary>A <c>list</c> of all the debug attributes that this device has.</summary>
        public readonly List<Attr> debug_attrs;

        /// <summary>A <c>list</c> of all the <see cref="iio.Channel"/> objects that this device possesses.</summary>
        public readonly List<Channel> channels;

        internal Device(Context ctx, IntPtr dev)
        {
            this.ctx = ctx;
            this.dev = dev;
            channels = new List<Channel>();
            attrs = new List<Attr>();
            debug_attrs = new List<Attr>();

            uint nb_channels = iio_device_get_channels_count(dev);
            uint nb_attrs = iio_device_get_attrs_count(dev);
            uint nb_debug_attrs = iio_device_get_debug_attrs_count(dev);

            for (uint i = 0; i < nb_channels; i++)
            {
                channels.Add(new Channel(this, iio_device_get_channel(dev, i)));
            }

            for (uint i = 0; i < nb_attrs; i++)
            {
                attrs.Add(new Attr(iio_device_get_attr(dev, i)));
            }

            for (uint i = 0; i < nb_debug_attrs; i++)
            {
                debug_attrs.Add(new Attr(iio_device_get_debug_attr(dev, i)));
            }

            id = Marshal.PtrToStringAnsi(iio_device_get_id(dev));

            IntPtr name_ptr = iio_device_get_name(dev);
            if (name_ptr == IntPtr.Zero)
            {
                name = "";
            }
            else
            {
                name = Marshal.PtrToStringAnsi(name_ptr);
            }

            IntPtr label_ptr = iio_device_get_label(dev);

            label = label_ptr == IntPtr.Zero ? "" : Marshal.PtrToStringAnsi(label_ptr);
            hwmon = id[0] == 'h';
        }

        /// <summary>Get the <see cref="iio.Channel"/> object of the specified name.</summary>
        /// <param name="name">Name or ID of the channel to look for</param>
        /// <param name="output">true if you are looking for an output channel, otherwise false.</param>
        /// <exception cref="IioLib.IIOException">The IIO device with the specified
        /// name or ID could not be found in the current context.</exception>
        public Channel get_channel(string name, bool output = false)
        {
            foreach (Channel each in channels)
            {
                if ((each.name.CompareTo(name) == 0 ||
                            each.id.CompareTo(name) == 0) && each.output == output)
                {
                    return each;
                }
            }

            throw new IIOException("Channel " + name + " not found");
        }

        /// <summary>Affect a trigger to this device.</summary>
        /// <param name="trig">A valid instance of the <see cref="iio.Trigger"/> class.</param>
        /// <exception cref="IioLib.IIOException">The trigger could not be set.</exception>
        public void set_trigger(Trigger trig)
        {
            int err = iio_device_set_trigger(this.dev, trig == null ? IntPtr.Zero : trig.dev);
            if (err < 0)
                throw new IIOException("Unable to set trigger", err);
        }

        /// <summary>Get the current trigger affected to this device.</summary>
        /// <returns>An instance of the <see cref="iio.Trigger"/> class.</returns>
        /// <exception cref="IioLib.IIOException">The instance could not be retrieved.</exception>
        public Trigger get_trigger()
        {
            IIOPtr ptr = iio_device_get_trigger(this.dev);
            if (!ptr)
            {
                 throw new IIOException("Unable to get trigger", ptr);
            }

            foreach (Trigger trig in ctx.devices)
            {
                if (trig.dev == ptr.ptr)
                {
                    return trig;
                }
            }

            return null;
        }

        /// <summary>Get the current sample size of the device.</summary>
        /// <remarks>The sample size varies each time channels get enabled or disabled.</remarks>
        /// <exception cref="IioLib.IIOException">Internal error. Please report any bug.</exception>
        public uint get_sample_size(ChannelsMask mask)
        {
            int ret = iio_device_get_sample_size(dev, mask.hdl);
            if (ret < 0)
                throw new IIOException("Unable to get sample size", ret);

            return (uint) ret;
        }
        /// <summary>Set a value to one register of this device.</summary>
        /// <param name="addr">The address of the register concerned.</param>
        /// <param name="value">The value that will be used for this register.</param>
        /// <exception cref="IioLib.IIOException">The register could not be written.</exception>
        public void reg_write(uint addr, uint value)
        {
            int err = iio_device_reg_write(dev, addr, value);
            if (err < 0)
                throw new IIOException("Unable to write register", err);
        }

        /// <summary>Read the content of a register of this device.</summary>
        /// <param name="addr">The address of the register concerned.</param>
        /// <exception cref="IioLib.IIOException">The register could not be read.</exception>
        public uint reg_read(uint addr)
        {
            uint value = 0;
            int err = iio_device_reg_read(dev, addr, ref value);
            if (err < 0)
                throw new IIOException("Unable to read register", err);

            return value;
        }
    }
}
