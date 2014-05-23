using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    public class Trigger : Device
    {
        public Trigger(Context ctx, IntPtr ptr) : base(ctx, ptr) { }

        public void set_rate(ulong rate)
        {
            foreach (Attr each in get_attrs())
                if (each.name().Equals("frequency"))
                {
                    each.write((long) rate);
                    return;
                }
            throw new Exception("Trigger has no frequency?");
        }

        public ulong get_rate()
        {
            foreach (Attr each in get_attrs())
                if (each.name().Equals("frequency"))
                    return (ulong) each.read_long();
            throw new Exception("Trigger has no frequency?");
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
