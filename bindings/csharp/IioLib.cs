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
    public struct IIOPtr
    {
        public readonly IntPtr ptr;

        public IIOPtr(IntPtr ptr)
        {
            this.ptr = ptr;
        }

        public int computeError()
        {
            int error;
            if (IntPtr.Size == 4)
            {
                error = (uint) ptr >= unchecked((uint) -4095) ? (int) ptr : 0;
            }
            else
            {
                error = (ulong) ptr >= unchecked((ulong) -4095L) ? (int)(long) ptr : 0;
            }
            return error;
        }

        /// <summary>Get a string representation of the error.</summary>
        public string str()
        {
            int error = computeError();
            return IioLib.strerror(error);
        }

        public static bool operator !(IIOPtr r)
        {
            int error = r.computeError();
            return error < 0;
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

    /// <summary>
    /// Represents the log levels for IIO.
    /// </summary>
    public enum IioLogLevel
    {
        NoLog = 1,
        Error = 2,
        Warning = 3,
        Info = 4,
        Debug = 5
    }

    /// <summary>
    /// Represents the parameters for creating an IIO context.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct IioContextParams
    {
        /// <summary>
        /// Handle to the standard output. If null, defaults to stdout.
        /// </summary>
        public IntPtr Out;

        /// <summary>
        /// Handle to the error output. If null, defaults to stderr.
        /// </summary>
        public IntPtr Err;

        /// <summary>
        /// Log level to use. Defaults to the log level specified at compilation.
        /// </summary>
        public IioLogLevel LogLevel;

        /// <summary>
        /// Log level threshold for sending messages to stderr.
        /// </summary>
        public IioLogLevel StderrLevel;

        /// <summary>
        /// Log level threshold for including timestamps in messages.
        /// </summary>
        public IioLogLevel TimestampLevel;

        /// <summary>
        /// Timeout for I/O operations in milliseconds. If zero, the default timeout is used.
        /// </summary>
        public uint TimeoutMs;

        /// <summary>
        /// Reserved for future fields. Must remain unused.
        /// </summary>
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public byte[] Reserved;
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
        private static extern bool iio_has_backend(IntPtr ctx_params, [In()] string backend);

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern int iio_get_builtin_backends_count();

        [DllImport(IioLib.dllname, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr iio_get_builtin_backend(uint index);

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

            return iio_has_backend(IntPtr.Zero, backend);
        }

        /// <summary>Checks if the given backend is available or not.</summary>
        /// <param name="ctx_params">Context parameters.</param>
        /// <param name="backend">The backend's name.</param>
        public static bool has_backend(IioContextParams ctx_params, string backend)
        {
            if (backend == null)
                throw new IIOException("The backend string should not be null!");

            // Allocate unmanaged memory for the structure
            IntPtr contextParamsPtr = Marshal.AllocHGlobal(Marshal.SizeOf<IioContextParams>());
            try
            {
                // Copy the managed structure to unmanaged memory
                Marshal.StructureToPtr(ctx_params, contextParamsPtr, false);

                // Call the native function
                return iio_has_backend(contextParamsPtr, backend);
            }
            finally
            {
                // Free the unmanaged memory
                Marshal.FreeHGlobal(contextParamsPtr);
            }
        }

        /// <summary>Gets the total number of available builtin backends.</summary>
        public static int get_builtin_backends_count()
        {
            return iio_get_builtin_backends_count();
        }

        /// <summary>Gets the builtin backend from the given index.</summary>
        public static string get_builtin_backend(uint index)
        {
            return UTF8Marshaler.PtrToStringUTF8(iio_get_builtin_backend(index));
        }
    }
}
