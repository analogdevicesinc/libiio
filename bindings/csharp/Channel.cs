using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    public class Channel
    {
        private class ChannelAttr : Attr
        {
            private IntPtr chn;

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_channel_attr_read(IntPtr chn, [In()] string name, [Out()] StringBuilder val, uint len);

            [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
            private static extern int iio_channel_attr_write(IntPtr chn, [In()] string name, string val);

            public ChannelAttr(IntPtr chn, string name) : base(name)
            {
                this.chn = chn;
            }

            public override string read()
            {
                StringBuilder builder = new StringBuilder(1024);
                int err = iio_channel_attr_read(chn, name, builder, (uint) builder.Capacity);
                if (err < 0)
                    throw new Exception("Unable to read channel attribute " + err);
                return builder.ToString();
            }

            public override void write(string str)
            {
                int err = iio_channel_attr_write(chn, name, str);
                if (err < 0)
                    throw new Exception("Unable to write channel attribute " + err);
            }
        }


        private IntPtr chn;
        private uint sample_size;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_channel_get_id(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_channel_get_name(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_channel_get_attrs_count(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_channel_get_attr(IntPtr chn, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_channel_is_output(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_channel_is_scan_element(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_channel_enable(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_channel_disable(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_channel_is_enabled(IntPtr chn);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_channel_read_raw(IntPtr chn, IntPtr buf, IntPtr dst, uint len);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_channel_write_raw(IntPtr chn, IntPtr buf, IntPtr src, uint len);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_channel_read(IntPtr chn, IntPtr buf, IntPtr dst, uint len);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_channel_write(IntPtr chn, IntPtr buf, IntPtr src, uint len);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_channel_get_data_format(IntPtr chn);

        public readonly string name;
        public readonly string id;
        public readonly bool output, scan_element;
        public readonly List<Attr> attrs;

        public Channel(IntPtr chn)
        {
            this.chn = chn;
            attrs = new List<Attr>();
            sample_size = (uint)Marshal.ReadInt32(iio_channel_get_data_format(this.chn)) / 8;
            uint nb_attrs = iio_channel_get_attrs_count(chn);

            for (uint i = 0; i < nb_attrs; i++)
                attrs.Add(new ChannelAttr(this.chn, Marshal.PtrToStringAnsi(iio_channel_get_attr(chn, i))));

            IntPtr name_ptr = iio_channel_get_name(this.chn);
            if (name_ptr == IntPtr.Zero)
                name = "";
            else
                name = Marshal.PtrToStringAnsi(name_ptr);

            id = Marshal.PtrToStringAnsi(iio_channel_get_id(this.chn));
            output = iio_channel_is_output(this.chn);
            scan_element = iio_channel_is_scan_element(this.chn);
        }

        public void enable()
        {
            iio_channel_enable(this.chn);
        }

        public void disable()
        {
            iio_channel_disable(this.chn);
        }

        public bool is_enabled()
        {
            return iio_channel_is_enabled(this.chn);
        }

        public byte[] read(IOBuffer buffer, bool raw = false)
        {
            if (!is_enabled())
                throw new Exception("Channel must be enabled before the IOBuffer is instantiated");
            if (this.output)
                throw new Exception("Unable to read from output channel");

            byte[] array = new byte[(int) (buffer.get_samples_count() * sample_size)];
            MemoryStream stream = new MemoryStream(array, true);
            GCHandle handle = GCHandle.Alloc(array, GCHandleType.Pinned);
            IntPtr addr = handle.AddrOfPinnedObject();
            uint count;

            if (raw)
                count = iio_channel_read_raw(this.chn, buffer.buf, addr, buffer.get_samples_count() * sample_size);
            else
                count = iio_channel_read(this.chn, buffer.buf, addr, buffer.get_samples_count() * sample_size);
            handle.Free();
            stream.SetLength((long) count);
            return stream.ToArray();

        }

        public uint write(IOBuffer buffer, byte[] array, bool raw = false)
        {
            if (!is_enabled())
                throw new Exception("Channel must be enabled before the IOBuffer is instantiated");
            if (!this.output)
                throw new Exception("Unable to write to an input channel");

            GCHandle handle = GCHandle.Alloc(array, GCHandleType.Pinned);
            IntPtr addr = handle.AddrOfPinnedObject();
            uint count;

            if (raw)
                count = iio_channel_write_raw(this.chn, buffer.buf, addr, (uint) array.Length);
            else
                count = iio_channel_write(this.chn, buffer.buf, addr, (uint) array.Length);
            handle.Free();

            return count;
        }
    }
}
