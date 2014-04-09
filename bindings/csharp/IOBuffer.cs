using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    class IOBuffer
    {
        public IntPtr buf;
        private bool is_output;
        private uint samples_count;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_create_buffer(IntPtr dev, uint samples_count, 
            [MarshalAs(UnmanagedType.I1)] bool is_output);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_buffer_destroy(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_refill(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_push(IntPtr buf);

        public IOBuffer(Device dev, uint samples_count, bool is_output = false)
        {
            this.is_output = is_output;
            this.samples_count = samples_count;

            buf = iio_device_create_buffer(dev.dev, samples_count, is_output);
            if (buf == null)
                throw new Exception("Unable to create buffer");
        }

        ~IOBuffer()
        {
            iio_buffer_destroy(buf);
        }

        public void refill()
        {
            if (this.is_output)
                throw new Exception("Impossible to refill an output buffer");

            int err = iio_buffer_refill(this.buf);
            if (err < 0)
                throw new Exception("Unable to refill buffer: err=" + err);
        }

        public void push()
        {
            if (!this.is_output)
                throw new Exception("Impossible to push an input buffer");

            int err = iio_buffer_push(this.buf);
            if (err < 0)
                throw new Exception("Unable to push buffer: err=" + err);
        }

        public uint get_samples_count()
        {
            return samples_count;
        }
    }
}
