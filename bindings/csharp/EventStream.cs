// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 * Author: Alexandra Trifan <Alexandra.Trifan@analog.com>
 */

using System;
using System.Runtime.InteropServices;

namespace iio
{
    /// <summary><see cref="iio.EventStream"/> class:
    /// Contains the representation of an event stream.</summary>
    public class EventStream : IIOObject
    {
        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_device_create_event_stream(IntPtr dev);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_event_stream_destroy(IntPtr stream);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_event_stream_read(IntPtr stream, IntPtr ev,
                    [MarshalAs(UnmanagedType.I1)] bool nonblock);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_event_get_channel(IntPtr ev, IntPtr dev,
                    [MarshalAs(UnmanagedType.I1)] bool diff);

        public readonly Device dev;

        public EventStream(Device device)
        {
            IIOPtr ptr = iio_device_create_event_stream(device.dev);
            if (!ptr)
                throw new IIOException("Unable to create iio.EventStream", ptr);

            this.hdl = ptr.ptr;
            this.dev = device;
        }

        /// <summary>Read an event from the event stream.</summary>
        /// <param name="nonblock">if True, the operation won't block and return -EBUSY if
        /// there is currently no event in the queue.</param>
        /// <remarks> It is possible to stop a blocking call of read_event
        /// by calling Destroy in a different thread.
        /// In that case, read_event will throw an exception.</remarks>
        /// <exception cref="IioLib.IIOException">Unable to read event.</exception>
        public IIOEvent read_event(bool nonblock)
        {
            IIOEventPtr iioeventptr;
            iioeventptr.id = 0;
            iioeventptr.timestamp = 0;
            IntPtr eventptr = Marshal.AllocHGlobal(Marshal.SizeOf(iioeventptr));

            int ret = iio_event_stream_read(hdl, eventptr, nonblock);
            if (ret < 0)
                throw new IIOException("Unable to read event", ret);

            IntPtr chnptr = iio_event_get_channel(eventptr, dev.dev, false);
            Channel chn = new Channel(dev, chnptr);
            IntPtr diffchnptr = iio_event_get_channel(eventptr, dev.dev, true);
            if (diffchnptr != IntPtr.Zero)
            {
                Channel chndiff = new Channel(dev, diffchnptr);
                return new IIOEvent(eventptr, chn, chndiff);
            }
            return new IIOEvent(eventptr, chn);
        }

        protected override void Destroy()
        {
            iio_event_stream_destroy(hdl);
        }
    }
}
