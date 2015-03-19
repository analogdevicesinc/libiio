using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    public class IOBuffer : IDisposable
    {
        private bool circular_buffer_pushed;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_device_create_buffer(IntPtr dev, uint samples_count,
                                  [MarshalAs(UnmanagedType.I1)] bool circular);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_buffer_destroy(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_refill(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_buffer_push(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_buffer_start(IntPtr buf);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_buffer_end(IntPtr buf);

        internal IntPtr buf;
        public readonly uint samples_count;
        public readonly bool circular;

        public IOBuffer(Device dev, uint samples_count, bool circular = false)
        {
            this.samples_count = samples_count;
            this.circular = circular;
            this.circular_buffer_pushed = false;

            buf = iio_device_create_buffer(dev.dev, samples_count, circular);
            if (buf == IntPtr.Zero)
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
            if (circular && circular_buffer_pushed)
                throw new Exception("Circular buffer already pushed\n");

            int err = iio_buffer_push(this.buf);
            if (err < 0)
                throw new Exception("Unable to push buffer: err=" + err);
            circular_buffer_pushed = true;
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

        public void fill(byte[] array)
        {
            int length = (int) iio_buffer_end(buf) - (int) iio_buffer_start(buf);
            if (length > array.Length)
                length = array.Length;
            Marshal.Copy(array, 0, iio_buffer_start(buf), length);
        }
    }
}
