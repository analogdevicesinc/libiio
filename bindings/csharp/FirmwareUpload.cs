// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

using System;
using System.Runtime.InteropServices;

namespace iio
{
    /// <summary>Progress of a firmware upload instance.</summary>
    public enum FwuStatus
    {
        /// <summary>No upload in progress.</summary>
        Idle = 0,
        /// <summary>The device is receiving the image.</summary>
        Receiving = 1,
        /// <summary>The device is preparing to program the image.</summary>
        Preparing = 2,
        /// <summary>The image is being transferred to the device.</summary>
        Transferring = 3,
        /// <summary>The device is being programmed.</summary>
        Programming = 4,
    }

    /// <summary><see cref="iio.FirmwareUpload"/> class:
    /// Contains the representation of a firmware upload instance of an IIO device.</summary>
    public class FirmwareUpload
    {
        // IIO_TIMEOUT_INFINITE and IIO_TIMEOUT_NONBLOCK
        private const int TimeoutInfinite = -1;
        private const int TimeoutNonblock = int.MinValue;

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_fwu_write(IntPtr dev, IntPtr fwu_id, byte[] data, UIntPtr len);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_fwu_wait(IntPtr dev, IntPtr fwu_id, int timeout_ms);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_fwu_get_status(IntPtr dev, IntPtr fwu_id, out int status, out ulong remaining);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_device_fwu_cancel(IntPtr dev, IntPtr fwu_id);

        private readonly Device dev;

        /// <summary>The ID of this firmware upload instance.</summary>
        public readonly string id;

        internal FirmwareUpload(Device dev, string id)
        {
            this.dev = dev;
            this.id = id;
        }

        private int call(Func<IntPtr, int> func)
        {
            IntPtr id_ptr = UTF8Marshaler.StringToHGlobalUTF8(id);
            try
            {
                return func(id_ptr);
            }
            finally
            {
                Marshal.FreeHGlobal(id_ptr);
            }
        }

        /// <summary>Get the progress of this firmware upload instance.</summary>
        /// <param name="remaining">The number of bytes that remain to be processed by the device.</param>
        /// <exception cref="IioLib.IIOException">The status could not be read.</exception>
        public FwuStatus get_status(out ulong remaining)
        {
            int status = 0;
            ulong rem = 0;
            int err = call(id_ptr => iio_device_fwu_get_status(dev.dev, id_ptr, out status, out rem));
            if (err < 0)
                throw new IIOException("Unable to get firmware upload status", err);

            remaining = rem;
            return (FwuStatus) status;
        }

        /// <summary>The progress of this firmware upload instance.</summary>
        public FwuStatus status
        {
            get
            {
                ulong remaining;
                return get_status(out remaining);
            }
        }

        /// <summary>Start uploading a firmware image.</summary>
        /// <remarks>This returns as soon as the image has been handed over to the device.
        /// Use <see cref="wait"/> to wait for the upload to complete.</remarks>
        /// <param name="data">The firmware image.</param>
        /// <exception cref="IioLib.IIOException">The image could not be uploaded.</exception>
        public void write(byte[] data)
        {
            if (data == null || data.Length == 0)
                throw new ArgumentException("Firmware image is empty", "data");

            int err = call(id_ptr => iio_device_fwu_write(dev.dev, id_ptr, data, (UIntPtr) data.Length));
            if (err < 0)
                throw new IIOException("Unable to write firmware", err);
        }

        /// <summary>Wait for a firmware upload to complete.</summary>
        /// <param name="timeout_ms">Maximum time to wait, in milliseconds. Wait until the upload
        /// completes if negative.</param>
        /// <param name="nonblock">If true, don't wait, and return false if the upload is still in progress.</param>
        /// <returns>true if the upload completed, false if it is still in progress
        /// (<paramref name="nonblock"/> is set, or <paramref name="timeout_ms"/> expired).
        /// The upload continues, and this method can be called again.</returns>
        /// <exception cref="IioLib.IIOException">The device reported an error.</exception>
        public bool wait(int timeout_ms = TimeoutInfinite, bool nonblock = false)
        {
            int timeout;

            if (nonblock)
            {
                timeout = TimeoutNonblock;
            }
            else if (timeout_ms < 0)
            {
                timeout = TimeoutInfinite;
            }
            else if (timeout_ms > 0)
            {
                timeout = timeout_ms;
            }
            else
            {
                throw new ArgumentOutOfRangeException("timeout_ms", "Timeout must not be zero");
            }

            int err = call(id_ptr => iio_device_fwu_wait(dev.dev, id_ptr, timeout));
            if (err < 0)
            {
                // The wait fails with EAGAIN while the upload is in progress.
                // The value of EAGAIN depends on the platform, so check the
                // status instead: the device is idle on any other error.
                if (status != FwuStatus.Idle)
                    return false;

                // The upload may have completed right after the wait gave up:
                // the device being idle, this returns the actual outcome.
                err = call(id_ptr => iio_device_fwu_wait(dev.dev, id_ptr, TimeoutNonblock));
                if (err < 0)
                    throw new IIOException("Firmware upload failed", err);
            }

            return true;
        }

        /// <summary>Upload a firmware image, and wait for the upload to complete.</summary>
        /// <param name="data">The firmware image.</param>
        /// <param name="timeout_ms">Maximum time to wait, in milliseconds. Wait until the upload
        /// completes if negative.</param>
        /// <returns>true if the upload completed, false if it is still in progress
        /// after <paramref name="timeout_ms"/>.</returns>
        /// <exception cref="IioLib.IIOException">The image could not be uploaded, or the
        /// device reported an error.</exception>
        public bool upload(byte[] data, int timeout_ms = TimeoutInfinite)
        {
            write(data);
            return wait(timeout_ms);
        }

        /// <summary>Cancel the firmware upload in progress.</summary>
        /// <exception cref="IioLib.IIOException">The upload could not be cancelled.</exception>
        public void cancel()
        {
            int err = call(id_ptr => iio_device_fwu_cancel(dev.dev, id_ptr));
            if (err < 0)
                throw new IIOException("Unable to cancel firmware upload", err);
        }
    }
}
