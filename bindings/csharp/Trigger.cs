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
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    /// <summary><see cref="iio.Trigger"/> class:
    /// Contains the representation of an IIO device that can act as a trigger.</summary>
    public class Trigger : Device
    {
        internal Trigger(Context ctx, IntPtr ptr) : base(ctx, ptr) { }

        /// <summary>Configure a new frequency for this trigger.</summary>
        /// <exception cref="System.Exception">The new frequency could not be set.</exception>
        public void set_rate(ulong rate)
        {
            foreach (Attr each in attrs)
                if (each.name.Equals("frequency"))
                {
                    each.write((long) rate);
                    return;
                }
            throw new Exception("Trigger has no frequency?");
        }

        /// <summary>Get the currently configured frequency of this trigger.</summary>
        /// <exception cref="System.Exception">The configured frequency could not be obtained.</exception>
        public ulong get_rate()
        {
            foreach (Attr each in attrs)
                if (each.name.Equals("frequency"))
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
