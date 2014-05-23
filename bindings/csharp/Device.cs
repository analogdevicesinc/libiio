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
            private Device dev;

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_device_attr_read(IntPtr dev, [In()] string name, [Out()] StringBuilder val, uint len);

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_device_attr_write(IntPtr dev, [In()] string name, [In()] string val);

            public DeviceAttr(Device dev, string name) : base(name)
            {
                this.dev = dev;
            }

            public override string read()
            {
                StringBuilder builder = new StringBuilder(1024);
                int err = iio_device_attr_read(dev.dev, attrname, builder, 1024);
                if (err < 0)
                    throw new Exception("Unable to read device attribute " + err);
                return builder.ToString();
            }

            public override void write(string str)
            {
                int err = iio_device_attr_write(dev.dev, attrname, str);
                if (err < 0)
                    throw new Exception("Unable to write device attribute " + err);
            }
        }

        private Context ctx;
        private List<Channel> channels;
        private List<DeviceAttr> attrs;
        public IntPtr dev;

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
        private static extern IntPtr iio_device_get_attr(IntPtr dev, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_get_trigger(IntPtr dev, IntPtr triggerptr);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_set_trigger(IntPtr dev, IntPtr trigger);

        public Device(Context ctx, IntPtr dev)
        {
            this.ctx = ctx;
            this.dev = dev;
            channels = new List<Channel>();
            attrs = new List<DeviceAttr>();
            uint nb_channels = iio_device_get_channels_count(dev),
                nb_attrs = iio_device_get_attrs_count(dev);

            for (uint i = 0; i < nb_channels; i++)
                channels.Add(new Channel(iio_device_get_channel(dev, i)));

            for (uint i = 0; i < nb_attrs; i++)
                attrs.Add(new DeviceAttr(this, Marshal.PtrToStringAnsi(iio_device_get_attr(dev, i))));
        }

        public string id()
        {
            return Marshal.PtrToStringAnsi(iio_device_get_id(dev));
        }

        public string name()
        {
            return Marshal.PtrToStringAnsi(iio_device_get_name(dev));
        }

        public List<Attr> get_attrs()
        {
            return attrs.ToList<Attr>();
        }

        public List<Channel> get_channels()
        {
            return channels;
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
            ptr = Marshal.ReadIntPtr(ptr);

            foreach (Trigger trig in ctx.get_devices()) {
                if (trig.dev == ptr)
                    return trig;
            }

            return null;
        }
    }
}
