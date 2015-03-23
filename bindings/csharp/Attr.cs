/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * */

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    /// <summary><see cref="iio.Attr"/> class:
    /// Contains the representation of a channel or device attribute.</summary>
    public abstract class Attr
    {
        /// <summary>The name of this attribute.</summary>
        public readonly string name;

        /// <summary>The filename in sysfs to which this attribute is bound.</summary>
        public readonly string filename;

        internal Attr(string name, string filename = null)
        {
            this.filename = filename == null ? name : filename;
            this.name = name;
        }

        /// <summary>Read the value of this attribute as a <c>string</c>.</summary>
        /// <exception cref="System.Exception">The attribute could not be read.</exception>
        public abstract string read();

        /// <summary>Set this attribute to the value contained in the <c>string</c> argument.</summary>
        /// <param name="val">The <c>string</c> value to set the parameter to.</param>
        /// <exception cref="System.Exception">The attribute could not be written.</exception>
        public abstract void write(string val);

        /// <summary>Read the value of this attribute as a <c>bool</c>.</summary>
        /// <exception cref="System.Exception">The attribute could not be read.</exception>
        public bool read_bool()
        {
            string val = read();
            return (val.CompareTo("1") == 0) || (val.CompareTo("Y") == 0);
        }

        /// <summary>Read the value of this attribute as a <c>double</c>.</summary>
        /// <exception cref="System.Exception">The attribute could not be read.</exception>
        public double read_double()
        {
            return double.Parse(read(), CultureInfo.InvariantCulture);
        }

        /// <summary>Read the value of this attribute as a <c>long</c>.</summary>
        /// <exception cref="System.Exception">The attribute could not be read.</exception>
        public long read_long()
        {
            return long.Parse(read(), CultureInfo.InvariantCulture);
        }

        /// <summary>Set this attribute to the value contained in the <c>bool</c> argument.</summary>
        /// <param name="val">The <c>bool</c> value to set the parameter to.</param>
        /// <exception cref="System.Exception">The attribute could not be written.</exception>
        public void write(bool val)
        {
            if (val)
                write("1");
            else
                write("0");
        }

        /// <summary>Set this attribute to the value contained in the <c>long</c> argument.</summary>
        /// <param name="val">The <c>long</c> value to set the parameter to.</param>
        /// <exception cref="System.Exception">The attribute could not be written.</exception>
        public void write(long val)
        {
            write(val.ToString(CultureInfo.InvariantCulture));
        }

        /// <summary>Set this attribute to the value contained in the <c>double</c> argument.</summary>
        /// <param name="val">The <c>double</c> value to set the parameter to.</param>
        /// <exception cref="System.Exception">The attribute could not be written.</exception>
        public void write(double val)
        {
            write(val.ToString(CultureInfo.InvariantCulture));
        }
    }
}
