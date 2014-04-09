using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    class Trigger : Device
    {
        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_trigger_set_rate(IntPtr dev, ulong rate);

        [DllImport("libiio.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_trigger_get_rate(IntPtr dev, IntPtr rate);

        public Trigger(Context ctx, IntPtr ptr) : base(ctx, ptr) { }

        public void set_rate(ulong rate)
        {
            int err = iio_trigger_set_rate(this.dev, rate);
            if (err != 0)
                throw new Exception("Unable to set rate: error " + err);
        }

        public ulong get_rate()
        {
            IntPtr ptr = (IntPtr) 0;
            int err = iio_trigger_get_rate(this.dev, ptr);
            if (err != 0)
                throw new Exception("Unable to set rate: error " + err);

            return (ulong) Marshal.ReadIntPtr(ptr);
        }

        public new void set_trigger(Trigger trig)
        {
            throw new InvalidComObjectException("Device is already a trigger");
        }

        public new Trigger get_trigger()
        {
            throw new InvalidComObjectException("Device is already a trigger");
        }
    }
}
