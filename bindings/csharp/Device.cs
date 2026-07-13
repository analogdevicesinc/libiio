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
        // P/Invoke declarations
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
        private static extern uint iio_device_get_event_attrs_count(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_attr(IntPtr dev, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_debug_attr(IntPtr dev, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_event_attr(IntPtr dev, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_device_get_trigger(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_set_trigger(IntPtr dev, IntPtr trigger);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_get_sample_size(IntPtr dev, IntPtr mask);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_device_get_buffers_count(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_buffer(IntPtr dev, uint index);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_reg_write(IntPtr dev, uint addr, uint value);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_reg_read(IntPtr dev, uint addr, ref uint value);

        // Internal fields
        internal IntPtr dev;

        // Public fields
        /// <summary>Gets the context of the current device.</summary>
        public readonly Context ctx;

        /// <summary>An identifier of this device.</summary>
        /// <remarks>The identifier is only valid in this IIO context</remarks>
        public readonly string id;

        /// <summary>The name of this device.</summary>
        public readonly string name;

        /// <summary>A <c>Dictionary</c> of all the attributes that this device has. Key is the attribute name.</summary>
        public readonly IReadOnlyDictionary<string, Attr> attrs;

        /// <summary>A <c>Dictionary</c> of all the debug attributes that this device has. Key is the attribute name.</summary>
        public readonly IReadOnlyDictionary<string, Attr> debug_attrs;

        /// <summary>A <c>Dictionary</c> of all the event attributes that this device has. Key is the attribute name.</summary>
        public readonly IReadOnlyDictionary<string, Attr> event_attrs;

        /// <summary>A <c>list</c> of all the <see cref="iio.Channel"/> objects that this device possesses.</summary>
        public readonly IReadOnlyList<Channel> channels;

        /// <summary>A <c>list</c> of all the <see cref="iio.IOBuffer"/> objects that this device possesses.</summary>
        public readonly IReadOnlyList<IOBuffer> buffers;

        // Public properties
        /// <summary>The label of this device.</summary>
        public string label { get; private set; }

        /// <summary>True if the device is a hardware monitoring device, False if it is a IIO device.</summary>
        public bool hwmon { get; private set; }

        // Constructor
        internal Device(Context ctx, IntPtr dev)
        {
            this.ctx = ctx;
            this.dev = dev;

            uint nb_channels = iio_device_get_channels_count(dev);
            uint nb_attrs = iio_device_get_attrs_count(dev);
            uint nb_debug_attrs = iio_device_get_debug_attrs_count(dev);
            uint nb_event_attrs = iio_device_get_event_attrs_count(dev);
            uint nb_buffers = iio_device_get_buffers_count(dev);

            var channelsList = new List<Channel>();
            for (uint i = 0; i < nb_channels; i++)
            {
                channelsList.Add(new Channel(this, iio_device_get_channel(dev, i)));
            }

            var attrsDict = new Dictionary<string, Attr>();
            for (uint i = 0; i < nb_attrs; i++)
            {
                Attr a = new Attr(iio_device_get_attr(dev, i));
                attrsDict[a.name] = a;
            }

            var debugAttrsDict = new Dictionary<string, Attr>();
            for (uint i = 0; i < nb_debug_attrs; i++)
            {
                Attr a = new Attr(iio_device_get_debug_attr(dev, i));
                debugAttrsDict[a.name] = a;
            }

            var eventAttrsDict = new Dictionary<string, Attr>();
            for (uint i = 0; i < nb_event_attrs; i++)
            {
                Attr a = new Attr(iio_device_get_event_attr(dev, i));
                eventAttrsDict[a.name] = a;
            }

            id = Marshal.PtrToStringAnsi(iio_device_get_id(dev)); // Device IDs are ASCII (kernel-defined)

            var buffersList = new List<IOBuffer>();
            for (uint i = 0; i < nb_buffers; i++)
            {
                buffersList.Add(new IOBuffer(this, iio_device_get_buffer(dev, i)));
            }

            channels = channelsList;
            attrs = attrsDict;
            debug_attrs = debugAttrsDict;
            event_attrs = eventAttrsDict;
            buffers = buffersList;

            IntPtr name_ptr = iio_device_get_name(dev);
            if (name_ptr == IntPtr.Zero)
            {
                name = "";
            }
            else
            {
                name = UTF8Marshaler.PtrToStringUTF8(name_ptr);
            }

            IntPtr label_ptr = iio_device_get_label(dev);

            label = label_ptr == IntPtr.Zero ? "" : UTF8Marshaler.PtrToStringUTF8(label_ptr);
            hwmon = id[0] == 'h';
        }

        // Public methods
        /// <summary>Get the <see cref="iio.Channel"/> object of the specified name.</summary>
        /// <param name="name">Name, ID, or label of the channel to look for</param>
        /// <param name="output">true if you are looking for an output channel, otherwise false.</param>
        /// <exception cref="IioLib.IIOException">The IIO channel with the specified
        /// name, ID, or label could not be found in the current device.</exception>
        public Channel get_channel(string name, bool output = false)
        {
            foreach (Channel each in channels)
            {
                if ((each.name.CompareTo(name) == 0 ||
                            each.id.CompareTo(name) == 0 ||
                            each.label.CompareTo(name) == 0) && each.output == output)
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
