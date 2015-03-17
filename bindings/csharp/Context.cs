using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    public class Version
    {
        public readonly uint major;
        public readonly uint minor;
        public readonly string git_tag;

        internal Version(uint major, uint minor, string git_tag)
        {
            this.major = major;
            this.minor = minor;
            this.git_tag = git_tag;
        }
    }

    public class Context : IDisposable
    {
        private IntPtr ctx;

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_create_network_context(
            [In()][MarshalAs(UnmanagedType.LPStr)] string hostname
        );

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_create_default_context();

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_context_destroy(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_name(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_description(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_xml(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_library_get_version(ref uint major, ref uint minor, [Out()] StringBuilder git_tag);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_context_get_version(IntPtr ctx, ref uint major, ref uint minor, [Out()] StringBuilder git_tag);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint iio_context_get_devices_count(IntPtr ctx);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_context_get_device(IntPtr ctx, uint index);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_device_is_trigger(IntPtr dev);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_context_set_timeout(IntPtr ctx, uint timeout_ms);

        public readonly string xml;
        public readonly string name;
        public readonly string description;
        public readonly Version library_version, backend_version;
        public readonly List<Device> devices;

        public Context(string hostname) : this(iio_create_network_context(hostname)) {}
        public Context() : this(iio_create_default_context()) {}

        private Context(IntPtr ctx)
        {
            this.ctx = ctx;

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

            uint major = 0;
            uint minor = 0;
            StringBuilder builder = new StringBuilder(8);
            int err = iio_library_get_version(ref major, ref minor, builder);
            if (err < 0)
                throw new Exception("Unable to read library version");
            library_version = new Version(major, minor, builder.ToString());

            major = 0;
            minor = 0;
            builder.Clear();
            err = iio_context_get_version(ctx, ref major, ref minor, builder);
            if (err < 0)
                throw new Exception("Unable to read backend version");
            backend_version = new Version(major, minor, builder.ToString());
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

        public void set_timeout(uint timeout)
        {
            int ret = iio_context_set_timeout(ctx, timeout);
            if (ret < 0)
                throw new Exception("Unable to set timeout");
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
