using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    public class Device
    {
        private class DeviceAttr : Attr
        {
            private IntPtr dev;

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
                    throw new Exception("Unable to read device attribute " + err);
                return builder.ToString();
            }

            public override void write(string str)
            {
                int err = iio_device_attr_write(dev, name, str);
                if (err < 0)
                    throw new Exception("Unable to write device attribute " + err);
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
                    throw new Exception("Unable to read debug attribute " + err);
                return builder.ToString();
            }

            public override void write(string str)
            {
                int err = iio_device_debug_attr_write(dev, name, str);
                if (err < 0)
                    throw new Exception("Unable to write debug attribute " + err);
            }
        }

        private Context ctx;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_id(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_name(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_device_get_channels_count(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_channel(IntPtr dev, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_device_get_attrs_count(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_device_get_debug_attrs_count(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_attr(IntPtr dev, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_get_debug_attr(IntPtr dev, uint index);

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

        internal IntPtr dev;
        public readonly string id;
        public readonly string name;
        public readonly List<Attr> attrs, debug_attrs;
        public readonly List<Channel> channels;

        public Device(Context ctx, IntPtr dev)
        {
            this.ctx = ctx;
            this.dev = dev;
            channels = new List<Channel>();
            attrs = new List<Attr>();
            debug_attrs = new List<Attr>();

            uint nb_channels = iio_device_get_channels_count(dev),
                nb_attrs = iio_device_get_attrs_count(dev),
                nb_debug_attrs = iio_device_get_debug_attrs_count(dev);

            for (uint i = 0; i < nb_channels; i++)
                channels.Add(new Channel(iio_device_get_channel(dev, i)));

            for (uint i = 0; i < nb_attrs; i++)
                attrs.Add(new DeviceAttr(dev, Marshal.PtrToStringAnsi(iio_device_get_attr(dev, i))));
            for (uint i = 0; i < nb_debug_attrs; i++)
                debug_attrs.Add(new DeviceDebugAttr(dev, Marshal.PtrToStringAnsi(iio_device_get_debug_attr(dev, i))));

            id = Marshal.PtrToStringAnsi(iio_device_get_id(dev));

            IntPtr name_ptr = iio_device_get_name(dev);
            if (name_ptr == IntPtr.Zero)
                name = "";
            else
                name = Marshal.PtrToStringAnsi(name_ptr);
        }

        public Channel get_channel(string name)
        {
            foreach (Channel each in channels) {
                if (each.name.CompareTo(name) == 0 ||
                            each.id.CompareTo(name) == 0)
                    return each;
            }

            throw new Exception("Channel " + name + " not found");
        }

        public void set_trigger(Trigger trig)
        {
            IntPtr ptr = (IntPtr) 0;
            int err = iio_device_set_trigger(trig.dev, ptr);
            if (err < 0)
                throw new Exception("Unable to set trigger: err=" + err);
        }

        public Trigger get_trigger()
        {
            IntPtr ptr = (IntPtr)0;
            int err = iio_device_get_trigger(this.dev, ptr);
            if (err < 0)
                throw new Exception("Unable to get trigger: err=" + err);

            ptr = Marshal.ReadIntPtr(ptr);

            foreach (Trigger trig in ctx.devices) {
                if (trig.dev == ptr)
                    return trig;
            }

            return null;
        }

        public uint get_sample_size()
        {
            int ret = iio_device_get_sample_size(dev);
            if (ret < 0)
                throw new Exception("Unable to get sample size: err=" + ret);
            return (uint) ret;
        }

        public void reg_write(uint addr, uint value)
        {
            int err = iio_device_reg_write(dev, addr, value);
            if (err < 0)
                throw new Exception("Unable to write register");
        }

        public uint reg_read(uint addr)
        {
            uint value = 0;
            int err = iio_device_reg_read(dev, addr, ref value);
            if (err < 0)
                throw new Exception("Unable to read register");
            return value;
        }
    }
}
