// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

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
        private const string FrequencyAttr = "frequency";

        internal Trigger(Context ctx, IntPtr ptr) : base(ctx, ptr) { }

        /// <summary>Configure a new frequency for this trigger.</summary>
        /// <exception cref="IioLib.IIOException">The new frequency could not be set.</exception>
        public void set_rate(ulong rate)
        {
            if (!attrs.ContainsKey(FrequencyAttr))
                throw new IIOException("Trigger has no frequency?");
            attrs[FrequencyAttr].write((long) rate);
        }

        /// <summary>Get the currently configured frequency of this trigger.</summary>
        /// <exception cref="IioLib.IIOException">The configured frequency could not be obtained.</exception>
        public ulong get_rate()
        {
            if (!attrs.ContainsKey(FrequencyAttr))
                throw new IIOException("Trigger has no frequency?");
            return (ulong) attrs[FrequencyAttr].read_long();
        }

        /// <summary>Set Trigger.</summary>
        public new void set_trigger(Trigger trig)
        {
            throw new IIOException("Device is already a trigger");
        }

        /// <summary>Get trigger.</summary>
        public new Trigger get_trigger()
        {
            throw new IIOException("Device is already a trigger");
        }
    }
}
