// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

using System;
using System.Runtime.InteropServices;

namespace iio
{
    public class ChannelsMask : IIOObject
    {
        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IIOPtr iio_create_channels_mask(uint nb_channels);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_channels_mask_destroy(IntPtr mask);

        public ChannelsMask(uint nb_channels)
        {
            IIOPtr ptr = iio_create_channels_mask(nb_channels);
            if (!ptr)
                throw new IIOException("Failed to create iio.ChannelsMask", ptr);

            this.hdl = ptr.ptr;
        }

        protected override void Destroy()
        {
            iio_channels_mask_destroy(hdl);
        }
    }
}
