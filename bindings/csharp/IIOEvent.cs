// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 * Author: Alexandra Trifan <Alexandra.Trifan@analog.com>
 */

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace iio
{
    [StructLayout(LayoutKind.Sequential)]
    public struct IIOEventPtr
    {
        public ulong id;
        public long timestamp;
    }

    /// <summary><see cref="iio.IIOEvent"/> class:
    /// Contains the representation of an iio_event.</summary>
    public class IIOEvent
    {
        /// <summary><see cref="iio.IIOEvent.EventType"/> class:
        /// Contains the available event types.</summary>
        public enum EventType
        {
            IIO_EV_TYPE_THRESH,
            IIO_EV_TYPE_MAG,
            IIO_EV_TYPE_ROC,
            IIO_EV_TYPE_THRESH_ADAPTIVE,
            IIO_EV_TYPE_MAG_ADAPTIVE,
            IIO_EV_TYPE_CHANGE,
            IIO_EV_TYPE_MAG_REFERENCED,
            IIO_EV_TYPE_GESTURE
        }

        /// <summary><see cref="iio.IIOEvent.EventDirection"/> class:
        /// Contains the available event directions.</summary>
        public enum EventDirection
        {
            IIO_EV_DIR_EITHER,
            IIO_EV_DIR_RISING,
            IIO_EV_DIR_FALLING,
            IIO_EV_DIR_NONE,
            IIO_EV_DIR_SINGLETAP,
            IIO_EV_DIR_DOUBLETAP
        }

        internal IntPtr ev;

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_event_get_type(IntPtr ev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_event_get_direction(IntPtr ev);

        internal IIOEventPtr event_ptr;
        public readonly Channel chn;
        public readonly Channel chn_diff;
        public readonly ulong id;
        public readonly long timestamp;

        /// <summary>The type of this event.</summary>
        public EventType type { get; private set; }

        /// <summary>Represents the direction of the event.</summary>
        public EventDirection direction { get; private set; }

        internal IIOEvent(IntPtr ev, Channel chn, Channel chndiff = null)
        {
            this.ev = ev;
            event_ptr = (IIOEventPtr) Marshal.PtrToStructure(ev, typeof(IIOEventPtr));
            this.id = event_ptr.id;
            this.timestamp = event_ptr.timestamp;
            this.chn = chn;
            this.chn_diff = chndiff;
            this.type = (EventType) ((this.id >> 56) & 0xff);
            this.direction = (EventDirection) ((this.id >> 48) & 0x7f);
        }
    }
}
