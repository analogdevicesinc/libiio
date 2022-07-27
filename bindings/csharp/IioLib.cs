// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2020 Analog Devices, Inc.
 * Author: Paul Cristian Iacob <cristian.iacob@analog.com>
 */

using System;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace iio
{
    public class IIOPtr
    {
        public readonly IntPtr ptr;
        public readonly int error;

        public IIOPtr(IntPtr ptr)
        {
            this.ptr = ptr;

            if (IntPtr.Size == 4) {
                this.error = (uint) ptr >= unchecked((uint) -4095) ? (int) ptr : 0;
            } else {
                this.error = (ulong) ptr >= unchecked((ulong) -4095L) ? (int)(long) ptr : 0;
            }
        }

        /// <summary>Get a string representation of the error.</summary>
        public string str()
        {
            return IioLib.strerror(error);
        }

        ~IIOPtr() {}

        public static bool operator !(IIOPtr r)
        {
            return r.error > 0;
        }
    }

    public class IIOException : Exception
    {
        public IIOException(string fmt) : base(fmt)
        {
        }

        public IIOException(string fmt, IIOPtr ptr)
            : base(string.Format("{0}: {1}", fmt, ptr.str()))
        {
        }

        public IIOException(string fmt, int err)
	    : base(string.Format("{0}: {1}", fmt, IioLib.strerror(err)))
        {
        }
    }

    public abstract class IIOObject : IDisposable
    {
        internal IntPtr hdl;

        protected IIOObject() : this(IntPtr.Zero) {}

        protected IIOObject(IntPtr hdl)
        {
            this.hdl = hdl;
        }

        ~IIOObject()
        {
            if (hdl != IntPtr.Zero)
                Dispose(false);
        }

        /// <summary>Releases all resource used by the <see cref="iio.IIOObject"/> object.</summary>
        /// <remarks>Call <see cref="Dispose"/> when you are finished using the <see cref="iio.IIOObject"/>. The
        /// <see cref="Dispose"/> method leaves the <see cref="iio.IIOObject"/> in an unusable state. After calling
        /// <see cref="Dispose"/>, you must release all references to the <see cref="iio.IIOObject"/> so the garbage
        /// collector can reclaim the memory that the <see cref="iio.IIOObject"/> was occupying.</remarks>
        public void Dispose()
        {
            Dispose(true);
        }

        private void Dispose(bool clean)
        {
            if (hdl != IntPtr.Zero)
            {
                if (clean)
                {
                    GC.SuppressFinalize(this);
                }
                Destroy();
                hdl = IntPtr.Zero;
            }
        }

        protected abstract void Destroy();
    }

    /// <summary><see cref="iio.IioLib"/> class:
    /// Contains the general methods from libiio.</summary>
    public static class IioLib
    {
        public const string dllname = "libiio1.dll";

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern void iio_strerror(int err, [Out()] StringBuilder buf, ulong len);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool iio_has_backend([In()] string backend);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_get_backends_count();

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_get_backend(uint index);

        /// <summary>Get a description of a negative error code.</summary>
        /// <param name="err">Negative error code.</param>
        public static string strerror(int err)
        {
            if (err > 0)
                throw new IIOException("strerror must only be called with negative error codes");

            StringBuilder builder = new StringBuilder(1024);

            iio_strerror(-err, builder, 1024);

            return builder.ToString();
        }

        /// <summary>Checks if the given backend is available or not.</summary>
        /// <param name="backend">The backend's name.</param>
        public static bool has_backend(string backend)
        {
            if (backend == null)
                throw new IIOException("The backend string should not be null!");

            return iio_has_backend(backend);
        }

        /// <summary>Gets the total number of available backends.</summary>
        public static int get_backends_count()
        {
            return iio_get_backends_count();
        }

        /// <summary>Gets the backend from the given index.</summary>
        public static string get_backend(uint index)
        {
            return Marshal.PtrToStringAnsi(iio_get_backend(index));
        }
    }
}
