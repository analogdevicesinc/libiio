using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    public class Context : IDisposable
    {
        private IntPtr ctx;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_create_network_context(
            [In()][MarshalAs(UnmanagedType.LPStr)] string hostname
        );

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_context_destroy(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_name(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_description(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_xml(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_context_get_devices_count(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_device(IntPtr ctx, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_device_is_trigger(IntPtr dev);

        public readonly string xml;
        public readonly string name;
        public readonly string description;
        public readonly List<Device> devices;

        public Context(string hostname)
        {
            ctx = iio_create_network_context(hostname);
            if (ctx == IntPtr.Zero)
                throw new Exception("Unable to create IIO context");

            uint nb_devices = iio_context_get_devices_count(ctx);

            devices = new List<Device>();
            for (uint i = 0; i < nb_devices; i++)
            {
                IntPtr ptr = iio_context_get_device(ctx, i);
                if (iio_device_is_trigger(ptr))
                    devices.Add(new Trigger(this, ptr));
                else
                    devices.Add(new Device(this, ptr));
            }

            xml = Marshal.PtrToStringAnsi(iio_context_get_xml(ctx));
            name = Marshal.PtrToStringAnsi(iio_context_get_name(ctx));
            description = Marshal.PtrToStringAnsi(iio_context_get_description(ctx));
        }

        ~Context()
        {
            if (ctx != IntPtr.Zero)
                Dispose(false);
        }

        public Device get_device(string name)
        {
            foreach (Device each in devices) {
                if (each.name.CompareTo(name) == 0 ||
                            each.id.CompareTo(name) == 0)
                    return each;
            }

            throw new Exception("Device " + name + " not found");
        }

        public void Dispose()
        {
            Dispose(true);
        }

        private void Dispose(bool clean)
        {
            if (ctx != IntPtr.Zero)
            {
                if (clean)
                    GC.SuppressFinalize(this);
                iio_context_destroy(ctx);
                ctx = IntPtr.Zero;
            }
        }
    }
}
