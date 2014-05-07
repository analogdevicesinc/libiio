using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    class IOBuffer : IDisposable
    {
        public IntPtr buf;
        private uint samples_count;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_create_buffer(IntPtr dev, uint samples_count);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_buffer_destroy(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_refill(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_push(IntPtr buf);

        public IOBuffer(Device dev, uint samples_count)
        {
            this.samples_count = samples_count;

            buf = iio_device_create_buffer(dev.dev, samples_count);
            if (buf == null)
                throw new Exception("Unable to create buffer");
        }

        ~IOBuffer()
        {
            if (buf != IntPtr.Zero)
                Dispose(false);
        }

        public void refill()
        {
            int err = iio_buffer_refill(this.buf);
            if (err < 0)
                throw new Exception("Unable to refill buffer: err=" + err);
        }

        public void push()
        {
            int err = iio_buffer_push(this.buf);
            if (err < 0)
                throw new Exception("Unable to push buffer: err=" + err);
        }

        public uint get_samples_count()
        {
            return samples_count;
        }

        public void Dispose()
        {
            Dispose(true);
        }

        private void Dispose(bool clean)
        {
            if (buf != IntPtr.Zero)
            {
                if (clean)
                    GC.SuppressFinalize(this);
                iio_buffer_destroy(buf);
                buf = IntPtr.Zero;
            }
        }
    }
}
