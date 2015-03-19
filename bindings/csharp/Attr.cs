using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    public abstract class Attr
    {
        public readonly string name;
        public readonly string filename;

        internal Attr(string name, string filename = null)
        {
            this.filename = filename == null ? name : filename;
            this.name = name;
        }

        public abstract string read();
        public abstract void write(string val);

        public bool read_bool()
        {
            string val = read();
            return (val.CompareTo("1") == 0) || (val.CompareTo("Y") == 0);
        }

        public double read_double()
        {
            return double.Parse(read(), CultureInfo.InvariantCulture);
        }

        public long read_long()
        {
            return long.Parse(read(), CultureInfo.InvariantCulture);
        }

        public void write(bool val)
        {
            if (val)
                write("1");
            else
                write("0");
        }

        public void write(long val)
        {
            write(val.ToString(CultureInfo.InvariantCulture));
        }

        public void write(double val)
        {
            write(val.ToString(CultureInfo.InvariantCulture));
        }
    }
}
