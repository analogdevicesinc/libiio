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
        private class DeviceAttr : Attr
        {
            internal IntPtr dev;

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_device_attr_read(IntPtr dev, [In()] string name, [Out()] StringBuilder val, uint len);

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_device_attr_write(IntPtr dev, [In()] string name, [In()] string val);

            public DeviceAttr(IntPtr dev, string name) : base(name)
            {
                this.dev = dev;
            }

            public override string read()
            {
                StringBuilder builder = new StringBuilder(1024);
                int err = iio_device_attr_read(dev, name, builder, 1024);
                if (err < 0)
                {
                    throw new Exception("Unable to read device attribute " + err);
                }
                return builder.ToString();
            }

            public override void write(string str)
            {
                int err = iio_device_attr_write(dev, name, str);
                if (err < 0)
                {
                    throw new Exception("Unable to write device attribute " + err);
                }
            }
        }

        private class DeviceDebugAttr : Attr
        {
            private IntPtr dev;

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_device_debug_attr_read(IntPtr dev, [In()] string name, [Out()] StringBuilder val, uint len);

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_device_debug_attr_write(IntPtr dev, [In()] string name, [In()] string val);

            public DeviceDebugAttr(IntPtr dev, string name) : base(name)
            {
                this.dev = dev;
            }

            public override string read()
            {
                StringBuilder builder = new StringBuilder(1024);
                int err = iio_device_debug_attr_read(dev, name, builder, 1024);
                if (err < 0)
                {
                    throw new Exception("Unable to read debug attribute " + err);
                }
                return builder.ToString();
            }

            public override void write(string str)
            {
                int err = iio_device_debug_attr_write(dev, name, str);
                if (err < 0)
                {
                    throw new Exception("Unable to write debug attribute " + err);
                }
            }
        }

        private class DeviceBufferAttr : Attr
        {
            private IntPtr dev;

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_device_buffer_attr_read(IntPtr dev, [In] string name, [Out] StringBuilder val, uint len);

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_device_buffer_attr_write(IntPtr dev, [In] string name, [In] string val);

            public DeviceBufferAttr(IntPtr dev, string name) : base(name)
            {
                this.dev = dev;
            }

            public override string read()
            {
                StringBuilder builder = new StringBuilder(16384);
                int err = iio_device_buffer_attr_read(dev, name, builder, 16384);
                if (err < 0)
                {
                    throw new Exception("Unable to read buffer attribute " + err);
                }
                return builder.ToString();
            }

            public override void write(string str)
            {
                int err = iio_device_buffer_attr_write(dev, name, str);
                if (err < 0)
                {
                    throw new Exception("Unable to write buffer attribute " + err);
                }
            }
        }

        internal Context ctx;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_id(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_name(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_label(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_device_get_channels_count(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_channel(IntPtr dev, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_device_get_attrs_count(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_device_get_debug_attrs_count(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_device_get_buffer_attrs_count(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_attr(IntPtr dev, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_debug_attr(IntPtr dev, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_buffer_attr(IntPtr dev, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_get_trigger(IntPtr dev, IntPtr triggerptr);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_set_trigger(IntPtr dev, IntPtr trigger);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_get_sample_size(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_reg_write(IntPtr dev, uint addr, uint value);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_reg_read(IntPtr dev, uint addr, ref uint value);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_context(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_set_kernel_buffers_count(IntPtr dev, uint nb);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_find_buffer_attr(IntPtr dev, [In] string name);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_find_debug_attr(IntPtr dev, [In] string name);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_find_attr(IntPtr dev, [In] string name);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_find_channel(IntPtr dev, [In] string name, [In] bool output);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_identify_filename(IntPtr dev, [In] string filename, out IntPtr chn_ptr, out IntPtr attr);

        internal IntPtr dev;

        /// <summary>An identifier of this device.</summary>
        /// <remarks>The identifier is only valid in this IIO context</remarks>
        public readonly string id;

        /// <summary>The name of this device.</summary>
        public readonly string name;

        /// <summary>The label of this device.</summary>
        public string label { get; private set; }

        /// <summary>A <c>list</c> of all the attributes that this device has.</summary>
        public readonly List<Attr> attrs;

        /// <summary>A <c>list</c> of all the debug attributes that this device has.</summary>
        public readonly List<Attr> debug_attrs;

        /// <summary>A <c>list</c> of all the buffer attributes that this device has.</summary>
        public List<Attr> buffer_attrs { get; private set; }

        /// <summary>A <c>list</c> of all the <see cref="iio.Channel"/> objects that this device possesses.</summary>
        public readonly List<Channel> channels;

        internal Device(Context ctx, IntPtr dev)
        {
            this.ctx = ctx;
            this.dev = dev;
            channels = new List<Channel>();
            attrs = new List<Attr>();
            debug_attrs = new List<Attr>();
            buffer_attrs = new List<Attr>();

            uint nb_channels = iio_device_get_channels_count(dev);
            uint nb_attrs = iio_device_get_attrs_count(dev);
            uint nb_debug_attrs = iio_device_get_debug_attrs_count(dev);
            uint nb_buffer_attrs = iio_device_get_buffer_attrs_count(dev);

            for (uint i = 0; i < nb_channels; i++)
            {
                channels.Add(new Channel(iio_device_get_channel(dev, i)));
            }

            for (uint i = 0; i < nb_attrs; i++)
            {
                attrs.Add(new DeviceAttr(dev, Marshal.PtrToStringAnsi(iio_device_get_attr(dev, i))));
            }

            for (uint i = 0; i < nb_debug_attrs; i++)
            {
                debug_attrs.Add(new DeviceDebugAttr(dev, Marshal.PtrToStringAnsi(iio_device_get_debug_attr(dev, i))));
            }

            for (uint i = 0; i < nb_buffer_attrs; i++)
            {
                buffer_attrs.Add(new DeviceBufferAttr(dev, Marshal.PtrToStringAnsi(iio_device_get_buffer_attr(dev, i))));
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
        }

        /// <summary>Get the <see cref="iio.Channel"/> object of the specified name.</summary>
        /// <param name="name">Name or ID of the channel to look for</param>
        /// <exception cref="System.Exception">The IIO device with the specified
        /// name or ID could not be found in the current context.</exception>
        public Channel get_channel(string name)
        {
            foreach (Channel each in channels)
            {
                if (each.name.CompareTo(name) == 0 ||
                            each.id.CompareTo(name) == 0)
                {
                    return each;
                }
            }

            throw new Exception("Channel " + name + " not found");
        }

        /// <summary>Affect a trigger to this device.</summary>
        /// <param name="trig">A valid instance of the <see cref="iio.Trigger"/> class.</param>
        /// <exception cref="System.Exception">The trigger could not be set.</exception>
        public void set_trigger(Trigger trig)
        {
            int err = iio_device_set_trigger(this.dev, trig == null ? IntPtr.Zero : trig.dev);
            if (err < 0)
            {
                throw new Exception("Unable to set trigger: err=" + err);
            }
        }

        /// <summary>Get the current trigger affected to this device.</summary>
        /// <returns>An instance of the <see cref="iio.Trigger"/> class.</returns>
        /// <exception cref="System.Exception">The instance could not be retrieved.</exception>
        public Trigger get_trigger()
        {
            IntPtr ptr = (IntPtr)0;
            int err = iio_device_get_trigger(this.dev, ptr);
            if (err < 0)
            {
                throw new Exception("Unable to get trigger: err=" + err);
            }

            ptr = Marshal.ReadIntPtr(ptr);

            foreach (Trigger trig in ctx.devices)
            {
                if (trig.dev == ptr)
                {
                    return trig;
                }
            }

            return null;
        }

        /// <summary>Get the current sample size of the device.</summary>
        /// <remarks>The sample size varies each time channels get enabled or disabled.</remarks>
        /// <exception cref="System.Exception">Internal error. Please report any bug.</exception>
        public uint get_sample_size()
        {
            int ret = iio_device_get_sample_size(dev);
            if (ret < 0)
            {
                throw new Exception("Internal error. Please report any bug.");
            }
            return (uint) ret;
        }
        /// <summary>Set a value to one register of this device.</summary>
        /// <param name="addr">The address of the register concerned.</param>
        /// <param name="value">The value that will be used for this register.</param>
        /// <exception cref="System.Exception">The register could not be written.</exception>
        public void reg_write(uint addr, uint value)
        {
            int err = iio_device_reg_write(dev, addr, value);
            if (err < 0)
            {
                throw new Exception("Unable to write register");
            }
        }

        /// <summary>Read the content of a register of this device.</summary>
        /// <param name="addr">The address of the register concerned.</param>
        /// <exception cref="System.Exception">The register could not be read.</exception>
        public uint reg_read(uint addr)
        {
            uint value = 0;
            int err = iio_device_reg_read(dev, addr, ref value);
            if (err < 0)
            {
                throw new Exception("Unable to read register");
            }
            return value;
        }

        /// <summary>Sets the number of active kernel buffers for this device.</summary>
        /// <param name="nb">The number of kernel buffers.</param>
        public int set_kernel_buffers_count(uint nb)
        {
            return iio_device_set_kernel_buffers_count(dev, nb);
        }

        /// <summary>Gets the context of the current device.</summary>
        /// <returns>An instance of the <see cref="iio.Context"/> class.</returns>
        public Context get_context()
        {
            return new Context(iio_device_get_context(dev));
        }

        /// <summary>Finds the channel with the given name from the current device.</summary>
        /// <param name="channel">The name of the channel.</param>
        /// <param name="output">true if you are looking for an output channel, otherwise false.</param>
        /// <returns>An instance of the <see cref="iio.Channel"/> class.</returns>
        /// <exception cref="System.Exception">There is no channel with the given name.</exception>
        public Channel find_channel(string channel, bool output)
        {
            IntPtr chn = iio_device_find_channel(dev, channel, output);

            if (chn == IntPtr.Zero)
            {
                throw new Exception("There is no channel with the given name!");
            }

            return new Channel(chn);
        }

        /// <summary>Finds the attribute with the given name from the current device.</summary>
        /// <param name="attribute">The name of the attribute.</param>
        /// <returns>An instance of the <see cref="iio.Device.DeviceAttr"/> class.</returns>
        /// <exception cref="System.Exception">There is no attribute with the given name.</exception>
        public Attr find_attribute(string attribute)
        {
            IntPtr attr = iio_device_find_attr(dev, attribute);

            if (attr == IntPtr.Zero)
            {
                throw new Exception("This device has no attribute with the given name!");
            }

            return new DeviceAttr(dev, Marshal.PtrToStringAnsi(attr));
        }

        /// <summary>Finds the debug attribute with the given name from the current device.</summary>
        /// <param name="attribute">The name of the debug attribute.</param>
        /// <returns>An instance of the <see cref="iio.Device.DeviceDebugAttr"/> class.</returns>
        /// <exception cref="System.Exception">There is no debug attribute with the given name.</exception>
        public Attr find_debug_attribute(string attribute)
        {
            IntPtr attr = iio_device_find_debug_attr(dev, attribute);

            if (attr == IntPtr.Zero)
            {
                throw new Exception("This device has no debug attribute with the given name!");
            }

            return new DeviceDebugAttr(dev, Marshal.PtrToStringAnsi(attr));
        }

        /// <summary>Finds the buffer attribute with the given name from the current device.</summary>
        /// <param name="attribute">The name of the buffer attribute.</param>
        /// <returns>An instance of the <see cref="iio.Device.DeviceBufferAttr"/> class.</returns>
        /// <exception cref="System.Exception">There is no attribute with the given name.</exception>
        public Attr find_buffer_attribute(string attribute)
        {
            IntPtr attr = iio_device_find_buffer_attr(dev, attribute);

            if (attr == IntPtr.Zero)
            {
                throw new Exception("This device has no buffer attribute with the given name!");
            }

            return new DeviceBufferAttr(dev, Marshal.PtrToStringAnsi(attr));
        }

        /// <summary>Finds the channel attribute coresponding to the given filename from the current device.</summary>
        /// <param name="filename">The name of the attribute.</param>
        /// <param name="chn_ptr">Output variable. It will contain a pointer to the resulting <see cref="iio.Channel"/>.</param>
        /// <param name="attr">Output variable. It will contain a pointer to the resulting <see cref="iio.ChannelAttr"/>.</param>
        /// <returns>C errorcode if error encountered, otherwise 0.</returns>
        public int identify_filename(string filename, IntPtr chn_ptr, IntPtr attr)
        {
            return iio_device_identify_filename(dev, filename, out chn_ptr, out attr);
        }
    }
}
