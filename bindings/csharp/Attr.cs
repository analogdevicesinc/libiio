using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    abstract class Attr
    {
        protected string attrname;

        protected Attr(string name)
        {
            this.attrname = name;
        }

        public string name()
        {
            return attrname;
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
            return Convert.ToDouble(read());
        }

        public long read_long()
        {
            return Convert.ToInt64(read());
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
            write("" + val);
        }

        public void write(double val)
        {
            write("" + val);
        }
    }
}
