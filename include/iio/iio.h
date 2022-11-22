/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

/** @file iio.h
 * @brief Public interface */

#ifndef __IIO_H__
#define __IIO_H__

#ifndef DOXYGEN

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#if (defined(_WIN32) || defined(__MBED__))
#ifndef _SSIZE_T_DEFINED
typedef ptrdiff_t ssize_t;
#define _SSIZE_T_DEFINED
#endif
#else
#include <sys/types.h>
#endif

#if defined(_MSC_VER) && (_MSC_VER < 1800) && !defined(__BOOL_DEFINED)
#undef bool
#undef false
#undef true
#define bool char
#define false 0
#define true 1
#else
#include <stdbool.h>
#endif

#if defined(__GNUC__) && !defined(MATLAB_MEX_FILE) && !defined(MATLAB_LOADLIBRARY)
#ifndef __cnst
#define __cnst __attribute__((const))
#endif
#ifndef __pure
#define __pure __attribute__((pure))
#endif
#define __notused __attribute__((unused))
#ifdef IIO_CHECK_RET
#define __check_ret __attribute__((warn_unused_result))
#else
#define __check_ret
#endif
#else
#define __cnst
#define __pure
#define __notused
#define __check_ret
#endif

#if !defined(_WIN32) && __GNUC__ >= 4 && \
	!defined(MATLAB_MEX_FILE) && !defined(MATLAB_LOADLIBRARY)
#  define __iio_api_export __attribute__((visibility ("default")))
#  define __iio_api_import
#elif defined(_WIN32) && !defined(LIBIIO_STATIC)
#  define __iio_api_export __declspec(dllexport)
#  define __iio_api_import __declspec(dllimport)
#else
#  define __iio_api_export
#  define __iio_api_import
#endif

#ifdef LIBIIO_EXPORTS
#  define __iio_api __iio_api_export
#else
#  define __iio_api __iio_api_import
#endif

#define __api __iio_api

#endif /* DOXYGEN */

struct iio_block;
struct iio_context;
struct iio_device;
struct iio_channel;
struct iio_channels_mask;
struct iio_buffer;
struct iio_scan;
struct iio_stream;

/**
 * @enum iio_log_level
 * @brief Level of verbosity of libiio's log output.
 */
enum iio_log_level {
	/* @brief No log output from the library */
	LEVEL_NOLOG = 1,

	/* @brief Only print error messages */
	LEVEL_ERROR = 2,

	/* @brief Also print warnings */
	LEVEL_WARNING = 3,

	/* @brief Also print noteworthy information */
	LEVEL_INFO = 4,

	/* @brief Also print debug messages */
	LEVEL_DEBUG = 5,
};

/**
 * @struct iio_context_params
 * @brief IIO context creation information
 *
 * This structure contains parameters that can affect how the IIO context is
 * created.
 */
struct iio_context_params {
	/** @brief Timeout for I/O operations. If zero, the default timeout is used. */
	unsigned int timeout_ms;

	/** @brief Handle to the standard output. If NULL, defaults to stdout. */
	FILE *out;

	/** @brief Handle to the error output. If NULL, defaults to stderr. */
	FILE *err;

	/** @brief Log level to use.
	 * Defaults to the log level that was specified at compilation. */
	enum iio_log_level log_level;

	/** @brief Under this log level (included), messages are sent to
	 * the error output ; above this log level (excluded), messages are
	 * sent to the standard output.
	 * If zero, defaults to LEVEL_WARNING. */
	enum iio_log_level stderr_level;
};

/*
 * <linux/iio/types.h> header guard to protect these enums from being defined
 * twice
 */
#ifndef _IIO_TYPES_H_
#define _IIO_TYPES_H_

/**
 * @enum iio_chan_type
 * @brief IIO channel type
 *
 * A IIO channel has a type specifying the type of data associated with the
 * channel.
 */
enum iio_chan_type {
	IIO_VOLTAGE,
	IIO_CURRENT,
	IIO_POWER,
	IIO_ACCEL,
	IIO_ANGL_VEL,
	IIO_MAGN,
	IIO_LIGHT,
	IIO_INTENSITY,
	IIO_PROXIMITY,
	IIO_TEMP,
	IIO_INCLI,
	IIO_ROT,
	IIO_ANGL,
	IIO_TIMESTAMP,
	IIO_CAPACITANCE,
	IIO_ALTVOLTAGE,
	IIO_CCT,
	IIO_PRESSURE,
	IIO_HUMIDITYRELATIVE,
	IIO_ACTIVITY,
	IIO_STEPS,
	IIO_ENERGY,
	IIO_DISTANCE,
	IIO_VELOCITY,
	IIO_CONCENTRATION,
	IIO_RESISTANCE,
	IIO_PH,
	IIO_UVINDEX,
	IIO_ELECTRICALCONDUCTIVITY,
	IIO_COUNT,
	IIO_INDEX,
	IIO_GRAVITY,
	IIO_POSITIONRELATIVE,
	IIO_PHASE,
	IIO_MASSCONCENTRATION,
	IIO_CHAN_TYPE_UNKNOWN = INT_MAX
};

/**
 * @enum iio_modifier
 * @brief IIO channel modifier
 *
 * In a addition to a type a IIO channel can optionally have a channel modifier
 * further specifying the data type of of the channel.
 */
enum iio_modifier {
	IIO_NO_MOD,
	IIO_MOD_X,
	IIO_MOD_Y,
	IIO_MOD_Z,
	IIO_MOD_X_AND_Y,
	IIO_MOD_X_AND_Z,
	IIO_MOD_Y_AND_Z,
	IIO_MOD_X_AND_Y_AND_Z,
	IIO_MOD_X_OR_Y,
	IIO_MOD_X_OR_Z,
	IIO_MOD_Y_OR_Z,
	IIO_MOD_X_OR_Y_OR_Z,
	IIO_MOD_LIGHT_BOTH,
	IIO_MOD_LIGHT_IR,
	IIO_MOD_ROOT_SUM_SQUARED_X_Y,
	IIO_MOD_SUM_SQUARED_X_Y_Z,
	IIO_MOD_LIGHT_CLEAR,
	IIO_MOD_LIGHT_RED,
	IIO_MOD_LIGHT_GREEN,
	IIO_MOD_LIGHT_BLUE,
	IIO_MOD_QUATERNION,
	IIO_MOD_TEMP_AMBIENT,
	IIO_MOD_TEMP_OBJECT,
	IIO_MOD_NORTH_MAGN,
	IIO_MOD_NORTH_TRUE,
	IIO_MOD_NORTH_MAGN_TILT_COMP,
	IIO_MOD_NORTH_TRUE_TILT_COMP,
	IIO_MOD_RUNNING,
	IIO_MOD_JOGGING,
	IIO_MOD_WALKING,
	IIO_MOD_STILL,
	IIO_MOD_ROOT_SUM_SQUARED_X_Y_Z,
	IIO_MOD_I,
	IIO_MOD_Q,
	IIO_MOD_CO2,
	IIO_MOD_VOC,
	IIO_MOD_LIGHT_UV,
	IIO_MOD_LIGHT_DUV,
	IIO_MOD_PM1,
	IIO_MOD_PM2P5,
	IIO_MOD_PM4,
	IIO_MOD_PM10,
	IIO_MOD_ETHANOL,
	IIO_MOD_H2,
	IIO_MOD_O2,
	IIO_MOD_LINEAR_X,
	IIO_MOD_LINEAR_Y,
	IIO_MOD_LINEAR_Z,
	IIO_MOD_PITCH,
	IIO_MOD_YAW,
	IIO_MOD_ROLL,
};

/**
 * @enum iio_event_type
 * @brief IIO event type
 *
 * Some IIO devices can deliver events. The type of the event can be specified
 * by one of the iio_event_type values.
 */
enum iio_event_type {
	IIO_EV_TYPE_THRESH,
	IIO_EV_TYPE_MAG,
	IIO_EV_TYPE_ROC,
	IIO_EV_TYPE_THRESH_ADAPTIVE,
	IIO_EV_TYPE_MAG_ADAPTIVE,
	IIO_EV_TYPE_CHANGE,
	IIO_EV_TYPE_MAG_REFERENCED,
	IIO_EV_TYPE_GESTURE,
};

/**
 * @enum iio_event_direction
 * @brief IIO event direction
 *
 * When applicable, this enum specifies the direction of the iio_event_type.
 */
enum iio_event_direction {
	IIO_EV_DIR_EITHER,
	IIO_EV_DIR_RISING,
	IIO_EV_DIR_FALLING,
	IIO_EV_DIR_NONE,
	IIO_EV_DIR_SINGLETAP,
	IIO_EV_DIR_DOUBLETAP,
};

#endif /* _IIO_TYPES_H_ */

/* ---------------------------------------------------------------------------*/
/* ---------------------------- Error handling -------------------------------*/
/** @defgroup Functions for handling pointer-encoded errors
 * @{
 * @brief Encode an error code into a pointer
 * @param err The error code to be encoded. Must be negative.
 * @return The error-encoding pointer. */
static inline __check_ret void *iio_ptr(int err)
{
	return (void *)(intptr_t) err;
}

/** @brief Returns the encoded error code if present, otherwise zero.
 * @param ptr Pointer that is either valid or error-encoding
 * @return The error code if present, otherwise zero. */
static inline __check_ret int iio_err(const void *ptr)
{
	return (uintptr_t) ptr >= (uintptr_t) -4095 ? (int)(intptr_t) ptr : 0;
}

/** @brief Type-cast an error-encoding pointer.
 * @param ptr Error-encoding pointer
 * @return An error-encoding pointer that can be used as a different type. */
static inline __check_ret void *iio_err_cast(const void *ptr)
{
	return (void *) ptr;
}

/** @} *//* ------------------------------------------------------------------*/
/* ------------------------- Scan functions ----------------------------------*/
/** @defgroup Scan Functions for scanning available contexts
 * @{
 * @struct iio_scan
 * @brief Structure holding scanning information
 */

/** @brief Scan backends for IIO contexts
 * @param params A pointer to a iio_context_params structure that contains
 *   context creation information; can be NULL
 * @param backends a NULL-terminated string containing a comma-separated list
 *   of the backends to be scanned for contexts. If NULL, all the available
 *   backends are scanned.
 * @return On success, a pointer to an iio_scan structure
 * @return On failure, a pointer-encoded error is returned
 *
 * <b>NOTE:</b> It is possible to provide backend-specific information.
 * For instance, "local,usb=0456:*" will scan the local backend and
 * limit scans on USB to vendor ID 0x0456, and accept all product IDs.
 * The "usb=0456:b673" string would limit the scan to the device with
 * this particular VID/PID. Both IDs are expected in hexadecimal, no 0x
 * prefix needed. */
__api __check_ret struct iio_scan *
iio_scan(const struct iio_context_params *params, const char *backends);


/** @brief Destroy the given scan context
 * @param ctx A pointer to an iio_scan structure
 *
 * <b>NOTE:</b> After that function, the iio_scan pointer shall be invalid. */
__api void iio_scan_destroy(struct iio_scan *ctx);


/** @brief Get number of results of a scan operation
 * @param ctx A pointer to an iio_scan structure
 * @return The number of results of the scan operation
 */
__api __check_ret __pure size_t
iio_scan_get_results_count(const struct iio_scan *ctx);


/** @brief Get description of scanned context
 * @param ctx A pointer to an iio_scan structure
 * @param idx The index of the scanned context
 * @return On success, a pointer to a NULL-terminated string
 * @return If the index is invalid, NULL is returned */
__api __check_ret __pure const char *
iio_scan_get_description(const struct iio_scan *ctx, size_t idx);


/** @brief Get URI of scanned context
 * @param ctx A pointer to an iio_scan structure
 * @param idx The index of the scanned context
 * @return On success, a pointer to a NULL-terminated string
 * @return If the index is invalid, NULL is returned */
__api __check_ret __pure const char *
iio_scan_get_uri(const struct iio_scan *ctx, size_t idx);


/** @} *//* ------------------------------------------------------------------*/
/* ------------------------- Top-level functions -----------------------------*/
/** @defgroup TopLevel Top-level functions
 * @{ */


/** @brief Get a string description of an error code
 * @param err The error code
 * @param dst A pointer to the memory area where the NULL-terminated string
 * corresponding to the error message will be stored
 * @param len The available length of the memory area, in bytes */
__api void iio_strerror(int err, char *dst, size_t len);


/** @brief Check if the specified backend is available
 * @param backend The name of the backend to query
 * @return True if the backend is available, false otherwise
 *
 * Introduced in version 0.9. */
__api __check_ret __cnst bool iio_has_backend(const char *backend);


/** @brief Get the number of available backends
 * @return The number of available backends
 *
 * Introduced in version 0.9. */
__api __check_ret __cnst unsigned int iio_get_backends_count(void);


/** @brief Retrieve the name of a given backend
 * @param index The index corresponding to the attribute
 * @return On success, a pointer to a static NULL-terminated string
 * @return If the index is invalid, NULL is returned
 *
 * Introduced in version 0.9. */
__api __check_ret __cnst const char * iio_get_backend(unsigned int index);


/** @} *//* ------------------------------------------------------------------*/
/* ------------------------- Context functions -------------------------------*/
/** @defgroup Context Context
 * @{
 * @struct iio_context
 * @brief Contains the representation of an IIO context */


/** @brief Create a context from a URI description
 * @param params A pointer to a iio_context_params structure that contains
 *   context creation information; can be NULL
 * @param uri a URI describing the context location. If NULL, the backend will
 *   be created using the URI string present in the IIOD_REMOTE environment
 *   variable, or if not set, a local backend is created.
 * @return On success, a pointer to a iio_context structure
 * @return On failure, a pointer-encoded error is returned
 *
 * <b>NOTE:</b> The following URIs are supported based on compile time backend
 * support:
 * - Local backend, "local:"\n
 *   Does not have an address part. For example <i>"local:"</i>
 * - XML backend, "xml:"\n Requires a path to the XML file for the address part.
 *   For example <i>"xml:/home/user/file.xml"</i>
 * - Network backend, "ip:"\n Requires a hostname, IPv4, or IPv6 to connect to
 *   a specific running IIO Daemon or no address part for automatic discovery
 *   when library is compiled with ZeroConf support. For example
 *   <i>"ip:192.168.2.1"</i>, <b>or</b> <i>"ip:localhost"</i>, <b>or</b> <i>"ip:"</i>
 *   <b>or</b> <i>"ip:plutosdr.local"</i>. To support alternative port numbers the
 *   standard <i>ip:host:port</i> format is used. A special format is required as
 *   defined in RFC2732 for IPv6 literal hostnames, (adding '[]' around the host)
 *   to use a <i>ip:[x:x:x:x:x:x:x:x]:port</i> format.
 *   Valid examples would be:
 *     - ip:                                               Any host on default port
 *     - ip::40000                                         Any host on port 40000
 *     - ip:analog.local                                   Default port
 *     - ip:brain.local:40000                              Port 40000
 *     - ip:192.168.1.119                                  Default Port
 *     - ip:192.168.1.119:40000                            Port 40000
 *     - ip:2601:190:400:da:47b3:55ab:3914:bff1            Default Port
 *     - ip:[2601:190:400:da:9a90:96ff:feb5:acaa]:40000    Port 40000
 * - USB backend, "usb:"\n When more than one usb device is attached, requires
 *   bus, address, and interface parts separated with a dot. For example
 *   <i>"usb:3.32.5"</i>. Where there is only one USB device attached, the shorthand
 *   <i>"usb:"</i> can be used.
 * - Serial backend, "serial:"\n Requires:
 *     - a port (/dev/ttyUSB0),
 *     - baud_rate (default <b>115200</b>)
 *     - serial port configuration
 *        - data bits (5 6 7 <b>8</b> 9)
 *        - parity ('<b>n</b>' none, 'o' odd, 'e' even, 'm' mark, 's' space)
 *        - stop bits (<b>1</b> 2)
 *        - flow control ('<b>\0</b>' none, 'x' Xon Xoff, 'r' RTSCTS, 'd' DTRDSR)
 *
 *  For example <i>"serial:/dev/ttyUSB0,115200"</i> <b>or</b> <i>"serial:/dev/ttyUSB0,115200,8n1"</i>*/
__api __check_ret struct iio_context *
iio_create_context(const struct iio_context_params *params, const char *uri);


/** @brief Destroy the given context
 * @param ctx A pointer to an iio_context structure
 *
 * <b>NOTE:</b> After that function, the iio_context pointer shall be invalid. */
__api void iio_context_destroy(struct iio_context *ctx);


/** @brief Get the major number of the library version
 * @param ctx Optional pointer to an iio_context structure
 * @return The major number
 *
 * NOTE: If ctx is non-null, it will return the major version of the remote
 * library, if running remotely. */
__api __pure unsigned int iio_context_get_version_major(const struct iio_context *ctx);


/** @brief Get the minor number of the library version
 * @param ctx Optional pointer to an iio_context structure
 * @return The minor number
 *
 * NOTE: If ctx is non-null, it will return the minor version of the remote
 * library, if running remotely. */
__api __pure unsigned int iio_context_get_version_minor(const struct iio_context *ctx);


/** @brief Get the git hash string of the library version
 * @param ctx Optional pointer to an iio_context structure
 * @return A NULL-terminated string that contains the git tag or hash
 *
 * NOTE: If ctx is non-null, it will return the git tag or hash of the remote
 * library, if running remotely. */
__api __pure const char * iio_context_get_version_tag(const struct iio_context *ctx);


/** @brief Obtain a XML representation of the given context
 * @param ctx A pointer to an iio_context structure
 * @return A pointer to a static NULL-terminated string */
__api __check_ret __pure const char * iio_context_get_xml(const struct iio_context *ctx);


/** @brief Get the name of the given context
 * @param ctx A pointer to an iio_context structure
 * @return A pointer to a static NULL-terminated string
 *
 * <b>NOTE:</b>The returned string will be <b><i>local</i></b>,
 * <b><i>xml</i></b> or <b><i>network</i></b> when the context has been
 * created with the local, xml and network backends respectively.*/
__api __check_ret __pure const char * iio_context_get_name(const struct iio_context *ctx);


/** @brief Get a description of the given context
 * @param ctx A pointer to an iio_context structure
 * @return A pointer to a static NULL-terminated string
 *
 * <b>NOTE:</b>The returned string will contain human-readable information about
 * the current context. */
__api __check_ret __pure const char * iio_context_get_description(
		const struct iio_context *ctx);


/** @brief Get the number of context-specific attributes
 * @param ctx A pointer to an iio_context structure
 * @return The number of context-specific attributes
 *
 * Introduced in version 0.9. */
__api __check_ret __pure unsigned int iio_context_get_attrs_count(
		const struct iio_context *ctx);


/** @brief Retrieve the name and value of a context-specific attribute
 * @param ctx A pointer to an iio_context structure
 * @param index The index corresponding to the attribute
 * @param name A pointer to a const char * pointer (NULL accepted)
 * @param value A pointer to a const char * pointer (NULL accepted)
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned
 *
 * Introduced in version 0.9. */
__api __check_ret int iio_context_get_attr(
		const struct iio_context *ctx, unsigned int index,
		const char **name, const char **value);


/** @brief Retrieve the value of a context-specific attribute
 * @param ctx A pointer to an iio_context structure
 * @param name The name of the context attribute to read
 * @return On success, a pointer to a static NULL-terminated string
 * @return If the name does not correspond to any attribute, NULL is
 * returned
 *
 * Introduced in version 0.9. */
__api __check_ret const char * iio_context_get_attr_value(
		const struct iio_context *ctx, const char *name);


/** @brief Enumerate the devices found in the given context
 * @param ctx A pointer to an iio_context structure
 * @return The number of devices found */
__api __check_ret __pure unsigned int iio_context_get_devices_count(
		const struct iio_context *ctx);


/** @brief Get the device present at the given index
 * @param ctx A pointer to an iio_context structure
 * @param index The index corresponding to the device
 * @return On success, a pointer to an iio_device structure
 * @return If the index is invalid, NULL is returned */
__api __check_ret __pure struct iio_device * iio_context_get_device(
		const struct iio_context *ctx, unsigned int index);


/** @brief Try to find a device structure by its ID, label or name
 * @param ctx A pointer to an iio_context structure
 * @param name A NULL-terminated string corresponding to the ID, label or name
 * of the device to search for
 * @return On success, a pointer to an iio_device structure
 * @return If the parameter does not correspond to the ID, label or name of
 * any known device, NULL is returned */
__api __check_ret __pure struct iio_device * iio_context_find_device(
		const struct iio_context *ctx, const char *name);


/** @brief Set a timeout for I/O operations
 * @param ctx A pointer to an iio_context structure
 * @param timeout_ms A positive integer representing the time in milliseconds
 * after which a timeout occurs. A value of 0 is used to specify that no
 * timeout should occur.
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
__api __check_ret int iio_context_set_timeout(
		struct iio_context *ctx, unsigned int timeout_ms);


/** @brief Get a pointer to the params structure
 * @param ctx A pointer to an iio_context structure
 * @return A pointer to the context's iio_context_params structure */
__api __cnst const struct iio_context_params *
iio_context_get_params(const struct iio_context *ctx);


/** @brief Associate a pointer to an iio_context structure
 * @param dev A pointer to an iio_context structure
 * @param data The pointer to be associated */
__api void iio_context_set_data(struct iio_context *ctx, void *data);


/** @brief Retrieve a previously associated pointer of an iio_context structure
 * @param dev A pointer to an iio_context structure
 * @return The pointer previously associated if present, or NULL */
__api void * iio_context_get_data(const struct iio_context *ctx);


/** @} *//* ------------------------------------------------------------------*/
/* ------------------------- Device functions --------------------------------*/
/** @defgroup Device Device
 * @{
 * @struct iio_device
 * @brief Represents a device in the IIO context */


/** @brief Retrieve a pointer to the iio_context structure
 * @param dev A pointer to an iio_device structure
 * @return A pointer to an iio_context structure */
__api __check_ret __pure const struct iio_context * iio_device_get_context(
		const struct iio_device *dev);


/** @brief Retrieve the device ID (e.g. <b><i>iio:device0</i></b>)
 * @param dev A pointer to an iio_device structure
 * @return A pointer to a static NULL-terminated string */
__api __check_ret __pure const char * iio_device_get_id(const struct iio_device *dev);


/** @brief Retrieve the device name (e.g. <b><i>xadc</i></b>)
 * @param dev A pointer to an iio_device structure
 * @return A pointer to a static NULL-terminated string
 *
 * <b>NOTE:</b> if the device has no name, NULL is returned. */
__api __check_ret __pure const char * iio_device_get_name(const struct iio_device *dev);


/** @brief Retrieve the device label (e.g. <b><i>lo_pll0_rx_adf4351</i></b>)
 * @param dev A pointer to an iio_device structure
 * @return A pointer to a static NULL-terminated string
 *
 * <b>NOTE:</b> if the device has no label, NULL is returned. */
__api __check_ret __pure const char * iio_device_get_label(const struct iio_device *dev);


/** @brief Enumerate the channels of the given device
 * @param dev A pointer to an iio_device structure
 * @return The number of channels found */
__api __check_ret __pure unsigned int iio_device_get_channels_count(
		const struct iio_device *dev);


/** @brief Enumerate the device-specific attributes of the given device
 * @param dev A pointer to an iio_device structure
 * @return The number of device-specific attributes found */
__api __check_ret __pure unsigned int iio_device_get_attrs_count(
		const struct iio_device *dev);

/** @brief Enumerate the buffer-specific attributes of the given device
 * @param dev A pointer to an iio_device structure
 * @return The number of buffer-specific attributes found */
__api __check_ret __pure unsigned int iio_device_get_buffer_attrs_count(
		const struct iio_device *dev);

/** @brief Get the channel present at the given index
 * @param dev A pointer to an iio_device structure
 * @param index The index corresponding to the channel
 * @return On success, a pointer to an iio_channel structure
 * @return If the index is invalid, NULL is returned */
__api __check_ret __pure struct iio_channel * iio_device_get_channel(
		const struct iio_device *dev, unsigned int index);


/** @brief Get the device-specific attribute present at the given index
 * @param dev A pointer to an iio_device structure
 * @param index The index corresponding to the attribute
 * @return On success, a pointer to a static NULL-terminated string
 * @return If the index is invalid, NULL is returned */
__api __check_ret __pure const char * iio_device_get_attr(
		const struct iio_device *dev, unsigned int index);

/** @brief Get the buffer-specific attribute present at the given index
 * @param dev A pointer to an iio_device structure
 * @param index The index corresponding to the attribute
 * @return On success, a pointer to a static NULL-terminated string
 * @return If the index is invalid, NULL is returned */
__api __check_ret __pure const char * iio_device_get_buffer_attr(
		const struct iio_device *dev, unsigned int index);

/** @brief Try to find a channel structure by its name of ID
 * @param dev A pointer to an iio_device structure
 * @param name A NULL-terminated string corresponding to the name or the ID of
 * the channel to search for
 * @param output True if the searched channel is output, False otherwise
 * @return On success, a pointer to an iio_channel structure
 * @return If the name or ID does not correspond to any known channel of the
 * given device, NULL is returned */
__api __check_ret __pure struct iio_channel * iio_device_find_channel(
		const struct iio_device *dev, const char *name, bool output);


/** @brief Try to find a device-specific attribute by its name
 * @param dev A pointer to an iio_device structure
 * @param name A NULL-terminated string corresponding to the name of the
 * attribute
 * @return On success, a pointer to a static NULL-terminated string
 * @return If the name does not correspond to any known attribute of the given
 * device, NULL is returned
 *
 * <b>NOTE:</b> This function is useful to detect the presence of an attribute.
 * It can also be used to retrieve the name of an attribute as a pointer to a
 * static string from a dynamically allocated string. */
__api __check_ret __pure const char * iio_device_find_attr(
		const struct iio_device *dev, const char *name);

/** @brief Try to find a buffer-specific attribute by its name
 * @param dev A pointer to an iio_device structure
 * @param name A NULL-terminated string corresponding to the name of the
 * attribute
 * @return On success, a pointer to a static NULL-terminated string
 * @return If the name does not correspond to any known attribute of the given
 * device, NULL is returned
 *
 * <b>NOTE:</b> This function is useful to detect the presence of an attribute.
 * It can also be used to retrieve the name of an attribute as a pointer to a
 * static string from a dynamically allocated string. */
__api __check_ret __pure const char * iio_device_find_buffer_attr(
		const struct iio_device *dev, const char *name);

/** @brief Read the content of the given device-specific attribute
 * @param dev A pointer to an iio_device structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param dst A pointer to the memory area where the NULL-terminated string
 * corresponding to the value read will be stored
 * @param len The available length of the memory area, in bytes
 * @return On success, the number of bytes written to the buffer
 * @return On error, a negative errno code is returned */
__api __check_ret ssize_t
iio_device_attr_read_raw(const struct iio_device *dev,
			 const char *attr, char *dst, size_t len);


/** @brief Read the content of the given device-specific attribute
 * @param dev A pointer to an iio_device structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param ptr A pointer to a variable where the value should be stored
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
#define iio_device_attr_read(dev, attr, ptr)			\
	_Generic((ptr),						\
		 bool *: iio_device_attr_read_bool,		\
		 long long *: iio_device_attr_read_longlong,	\
		 double *: iio_device_attr_read_double)(dev, attr, ptr)


/** @brief Set the value of the given device-specific attribute
 * @param dev A pointer to an iio_device structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param src A pointer to the data to be written
 * @param len The number of bytes that should be written
 * @return On success, the number of bytes written
 * @return On error, a negative errno code is returned */
__api __check_ret ssize_t
iio_device_attr_write_raw(const struct iio_device *dev,
			  const char *attr, const void *src, size_t len);


/** @brief Set the value of the given device-specific attribute
 * @param dev A pointer to an iio_device structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param val The value to set the attribute to
 * @return On success, the number of bytes written
 * @return On error, a negative errno code is returned. */
#define iio_device_attr_write(dev, attr, val)			\
	_Generic((val),						\
		 const char *: iio_device_attr_write_string,	\
		 char *: iio_device_attr_write_string,		\
		 bool: iio_device_attr_write_bool,		\
		 long long: iio_device_attr_write_longlong,	\
		 double: iio_device_attr_write_double)(dev, attr, val)


/** @brief Read the content of the given buffer-specific attribute
 * @param dev A pointer to an iio_device structure
 * @param buf_id The index of the hardware buffer (generally 0)
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param dst A pointer to the memory area where the NULL-terminated string
 * corresponding to the value read will be stored
 * @param len The available length of the memory area, in bytes
 * @return On success, the number of bytes written to the buffer
 * @return On error, a negative errno code is returned */
__api __check_ret ssize_t
iio_device_buffer_attr_read_raw(const struct iio_device *dev,
				unsigned int buf_id,
				const char *attr, char *dst, size_t len);


/** @brief Read the content of the given buffer-specific attribute
 * @param dev A pointer to an iio_device structure
 * @param buf_id The index of the hardware buffer (generally 0)
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param ptr A pointer to the variable where the value should be stored
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
#define iio_device_buffer_attr_read(dev, buf_id, attr, ptr)		\
	_Generic((ptr),							\
		 bool *: iio_device_buffer_attr_read_bool,		\
		 long long *: iio_device_buffer_attr_read_longlong,	\
		 double *: iio_device_buffer_attr_read_double)(dev, buf_id, attr, ptr)


/** @brief Set the value of the given buffer-specific attribute
 * @param dev A pointer to an iio_device structure
 * @param buf_id The index of the hardware buffer (generally 0)
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param src A pointer to the data to be written
 * @param len The number of bytes that should be written
 * @return On success, the number of bytes written
 * @return On error, a negative errno code is returned */
__api __check_ret ssize_t
iio_device_buffer_attr_write_raw(const struct iio_device *dev,
				 unsigned int buf_id, const char *attr,
				 const void *src, size_t len);


/** @brief Set the value of the given buffer-specific attribute
 * @param dev A pointer to an iio_device structure
 * @param buf_id The index of the hardware buffer (generally 0)
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param val A long long value to set the attribute to
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
#define iio_device_buffer_attr_write(dev, buf_id, attr, val)		\
	_Generic((val),							\
		 const char *: iio_device_buffer_attr_write_string,	\
		 char *: iio_device_buffer_attr_write_string,		\
		 bool: iio_device_buffer_attr_write_bool,		\
		 long long: iio_device_buffer_attr_write_longlong,	\
		 double: iio_device_buffer_attr_write_double)(dev, buf_id, attr, val)


/** @brief Associate a pointer to an iio_device structure
 * @param dev A pointer to an iio_device structure
 * @param data The pointer to be associated */
__api void iio_device_set_data(struct iio_device *dev, void *data);


/** @brief Retrieve a previously associated pointer of an iio_device structure
 * @param dev A pointer to an iio_device structure
 * @return The pointer previously associated if present, or NULL */
__api void * iio_device_get_data(const struct iio_device *dev);


/** @brief Retrieve the trigger of a given device
 * @param dev A pointer to an iio_device structure
 * @param trigger a pointer to a pointer of an iio_device structure. The pointed
 * pointer will be set to the address of the iio_device structure corresponding
 * to the associated trigger device.
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
__api __check_ret int iio_device_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger);


/** @brief Associate a trigger to a given device
 * @param dev A pointer to an iio_device structure
 * @param trigger a pointer to the iio_device structure corresponding to the
 * trigger that should be associated.
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
__api __check_ret int iio_device_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger);


/** @brief Return True if the given device is a trigger
 * @param dev A pointer to an iio_device structure
 * @return True if the device is a trigger, False otherwise */
__api __check_ret __pure bool iio_device_is_trigger(const struct iio_device *dev);

/** @brief Configure the number of kernel buffers for a device
 *
 * This function allows to change the number of buffers on kernel side.
 * @param dev A pointer to an iio_device structure
 * @param nb_buffers The number of buffers
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
__api __check_ret int iio_device_set_kernel_buffers_count(const struct iio_device *dev,
		unsigned int nb_buffers);

/** @} *//* ------------------------------------------------------------------*/
/* ------------------------- Channel functions -------------------------------*/
/** @defgroup Channel Channel
 * @{
 * @struct iio_channel
 * @brief Represents an input or output channel of a device */


/** @brief Retrieve a pointer to the iio_device structure
 * @param chn A pointer to an iio_channel structure
 * @return A pointer to an iio_device structure */
__api __check_ret __pure const struct iio_device * iio_channel_get_device(
		const struct iio_channel *chn);


/** @brief Retrieve the channel ID (e.g. <b><i>voltage0</i></b>)
 * @param chn A pointer to an iio_channel structure
 * @return A pointer to a static NULL-terminated string */
__api __check_ret __pure const char * iio_channel_get_id(const struct iio_channel *chn);


/** @brief Retrieve the channel name (e.g. <b><i>vccint</i></b>)
 * @param chn A pointer to an iio_channel structure
 * @return A pointer to a static NULL-terminated string
 *
 * <b>NOTE:</b> if the channel has no name, NULL is returned. */
__api __check_ret __pure const char * iio_channel_get_name(const struct iio_channel *chn);


/** @brief Return True if the given channel is an output channel
 * @param chn A pointer to an iio_channel structure
 * @return True if the channel is an output channel, False otherwise */
__api __check_ret __pure bool iio_channel_is_output(const struct iio_channel *chn);


/** @brief Return True if the given channel is a scan element
 * @param chn A pointer to an iio_channel structure
 * @return True if the channel is a scan element, False otherwise
 *
 * <b>NOTE:</b> a channel that is a scan element is a channel that can
 * generate samples (for an input channel) or receive samples (for an output
 * channel) after being enabled. */
__api __check_ret __pure bool iio_channel_is_scan_element(const struct iio_channel *chn);


/** @brief Enumerate the channel-specific attributes of the given channel
 * @param chn A pointer to an iio_channel structure
 * @return The number of channel-specific attributes found */
__api __check_ret __pure unsigned int iio_channel_get_attrs_count(
		const struct iio_channel *chn);


/** @brief Get the channel-specific attribute present at the given index
 * @param chn A pointer to an iio_channel structure
 * @param index The index corresponding to the attribute
 * @return On success, a pointer to a static NULL-terminated string
 * @return If the index is invalid, NULL is returned */
__api __check_ret __pure const char * iio_channel_get_attr(
		const struct iio_channel *chn, unsigned int index);


/** @brief Try to find a channel-specific attribute by its name
 * @param chn A pointer to an iio_channel structure
 * @param name A NULL-terminated string corresponding to the name of the
 * attribute
 * @return On success, a pointer to a static NULL-terminated string
 * @return If the name does not correspond to any known attribute of the given
 * channel, NULL is returned
 *
 * <b>NOTE:</b> This function is useful to detect the presence of an attribute.
 * It can also be used to retrieve the name of an attribute as a pointer to a
 * static string from a dynamically allocated string. */
__api __check_ret __pure const char * iio_channel_find_attr(
		const struct iio_channel *chn, const char *name);


/** @brief Retrieve the filename of an attribute
 * @param chn A pointer to an iio_channel structure
 * @param attr a NULL-terminated string corresponding to the name of the
 * attribute
 * @return On success, a pointer to a static NULL-terminated string
 * @return If the attribute name is unknown, NULL is returned */
__api __check_ret __pure const char * iio_channel_attr_get_filename(
		const struct iio_channel *chn, const char *attr);


/** @brief Read the content of the given channel-specific attribute
 * @param chn A pointer to an iio_channel structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param dst A pointer to the memory area where the NULL-terminated string
 * corresponding to the value read will be stored
 * @param len The available length of the memory area, in bytes
 * @return On success, the number of bytes written to the buffer
 * @return On error, a negative errno code is returned */
__api __check_ret ssize_t
iio_channel_attr_read_raw(const struct iio_channel *chn,
			  const char *attr, char *dst, size_t len);


/** @brief Read the content of the given channel-specific attribute
 * @param chn A pointer to an iio_channel structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param ptr A pointer to the variable where the value should be stored
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
#define iio_channel_attr_read(chn, attr, ptr)			\
	_Generic((ptr),						\
		 bool *: iio_channel_attr_read_bool,		\
		 long long *: iio_channel_attr_read_longlong,	\
		 double *: iio_channel_attr_read_double)(chn, attr, ptr)


/** @brief Set the value of the given channel-specific attribute
 * @param chn A pointer to an iio_channel structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param src A pointer to the data to be written
 * @param len The number of bytes that should be written
 * @return On success, the number of bytes written
 * @return On error, a negative errno code is returned */
__api __check_ret ssize_t
iio_channel_attr_write_raw(const struct iio_channel *chn,
			   const char *attr, const void *src, size_t len);


/** @brief Set the value of the given channel-specific attribute
 * @param chn A pointer to an iio_channel structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * attribute
 * @param val The value to set the attribute to
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
#define iio_channel_attr_write(chn, attr, val)			\
	_Generic((val),						\
		 const char *: iio_channel_attr_write_string,	\
		 char *: iio_channel_attr_write_string,		\
		 bool: iio_channel_attr_write_bool,		\
		 long long: iio_channel_attr_write_longlong,	\
		 double: iio_channel_attr_write_double)(chn, attr, val)


/** @brief Enable the given channel
 * @param chn A pointer to an iio_channel structure
 * @param mask The channels mask to manipulate */
__api void iio_channel_enable(const struct iio_channel *chn,
			      struct iio_channels_mask *mask);


/** @brief Disable the given channel
 * @param chn A pointer to an iio_channel structure
 * @param mask The channels mask to manipulate */
__api void iio_channel_disable(const struct iio_channel *chn,
			       struct iio_channels_mask *mask);


/** @brief Returns True if the channel is enabled
 * @param chn A pointer to an iio_channel structure
 * @return True if the channel is enabled, False otherwise */
__api __check_ret bool
iio_channel_is_enabled(const struct iio_channel *chn,
		       const struct iio_channels_mask *mask);


/** @brief Demultiplex and convert the samples of a given channel
 * @param chn A pointer to an iio_channel structure
 * @param block A pointer to an iio_block structure
 * @param dst A pointer to the memory area where the converted data will be
 * stored
 * @param len The available length of the memory area, in bytes
 * @param raw True to read samples in the hardware format, false to read
 *     converted samples.
 * @return The size of the converted data, in bytes */
__api __check_ret size_t iio_channel_read(const struct iio_channel *chn,
					  const struct iio_block *block,
					  void *dst, size_t len, bool raw);


/** @brief Convert and multiplex the samples of a given channel
 * @param chn A pointer to an iio_channel structure
 * @param block A pointer to an iio_block structure
 * @param src A pointer to the memory area where the sequential data will
 * be read from
 * @param len The length of the memory area, in bytes
 * @param raw True if the samples are already in hardware format, false if they
 *     need to be converted.
 * @return The number of bytes actually converted and multiplexed */
__api __check_ret size_t iio_channel_write(const struct iio_channel *chn,
					   struct iio_block *block,
					   const void *src, size_t len,
					   bool raw);


/** @brief Associate a pointer to an iio_channel structure
 * @param chn A pointer to an iio_channel structure
 * @param data The pointer to be associated */
__api void iio_channel_set_data(struct iio_channel *chn, void *data);


/** @brief Retrieve a previously associated pointer of an iio_channel structure
 * @param chn A pointer to an iio_channel structure
 * @return The pointer previously associated if present, or NULL */
__api void * iio_channel_get_data(const struct iio_channel *chn);


/** @brief Get the type of the given channel
 * @param chn A pointer to an iio_channel structure
 * @return The type of the channel */
__api __check_ret __pure enum iio_chan_type iio_channel_get_type(
		const struct iio_channel *chn);


/** @brief Get the modifier type of the given channel
 * @param chn A pointer to an iio_channel structure
 * @return The modifier type of the channel */
__api __check_ret __pure enum iio_modifier iio_channel_get_modifier(
		const struct iio_channel *chn);


/** @} *//* ------------------------------------------------------------------*/
/* ------------------------- Buffer functions --------------------------------*/
/** @defgroup Buffer Buffer
 * @{
 * @struct iio_buffer
 * @brief An input or output buffer, used to read or write samples */


/** @brief Retrieve a pointer to the iio_device structure
 * @param buf A pointer to an iio_buffer structure
 * @return A pointer to an iio_device structure */
__api __check_ret __pure const struct iio_device * iio_buffer_get_device(
		const struct iio_buffer *buf);


/** @brief Create an input or output buffer associated to the given device
 * @param dev A pointer to an iio_device structure
 * @param idx The index of the hardware buffer. Should be 0 in most cases.
 * @param mask A pointer to an iio_channels_mask structure
 * @return On success, a pointer to an iio_buffer structure
 * @return On failure, a pointer-encoded error is returned */
__api __check_ret struct iio_buffer *
iio_device_create_buffer(const struct iio_device *dev, unsigned int idx,
			 const struct iio_channels_mask *mask);


/** @brief Associate a pointer to an iio_buffer structure
 * @param buf A pointer to an iio_buffer structure
 * @param data The pointer to be associated */
__api void iio_buffer_set_data(struct iio_buffer *buf, void *data);


/** @brief Retrieve a previously associated pointer of an iio_buffer structure
 * @param buf A pointer to an iio_buffer structure
 * @return The pointer previously associated if present, or NULL */
__api void * iio_buffer_get_data(const struct iio_buffer *buf);


/** @brief Destroy the given buffer
 * @param buf A pointer to an iio_buffer structure */
__api void iio_buffer_destroy(struct iio_buffer *buf);


/** @brief Cancel all buffer operations
 * @param buf The buffer for which operations should be canceled
 *
 * This function cancels all outstanding buffer operations previously scheduled.
 * This means that any pending iio_block_enqueue() or iio_block_dequeue()
 * operation will abort and return immediately, any further invocation of these
 * functions on the same buffer will return immediately with an error.
 *
 * Usually iio_block_dequeue() will block until all data has been transferred
 * or a timeout occurs. This can depending on the configuration take a
 * significant amount of time. iio_buffer_cancel() is useful to bypass these
 * conditions if the buffer operation is supposed to be stopped in response to
 * an external event (e.g. user input).
 *
 * To be able to transfer additional data after calling this function the buffer
 * should be destroyed and then re-created.
 *
 * This function can be called multiple times for the same buffer, but all but
 * the first invocation will be without additional effect.
 *
 * This function is thread-safe, but not signal-safe, i.e. it must not be called
 * from a signal handler.
 */
__api void iio_buffer_cancel(struct iio_buffer *buf);


/** @brief Enable the buffer
 * @param buf A pointer to an iio_buffer structure
 * @return On success, 0
 * @return On error, a negative error code is returned */
__api __check_ret int iio_buffer_enable(struct iio_buffer *buf);


/** @brief Disable the buffer
 * @param buf A pointer to an iio_buffer structure
 * @return On success, 0
 * @return On error, a negative error code is returned */
__api int iio_buffer_disable(struct iio_buffer *buf);


/** @brief Retrieve a mask of the channels enabled for the given buffer
 * @param buf A pointer to an iio_buffer structure
 * @return A pointer to an iio_channels_mask structure
 *
 * <b>NOTE:</b> The mask returned may contain more enabled channels than
 * the mask used for creating the buffer. */
__api const struct iio_channels_mask *
iio_buffer_get_channels_mask(const struct iio_buffer *buf);


/** @} *//* ------------------------------------------------------------------*/
/* -------------------------- Block functions --------------------------------*/
/** @defgroup Block Block
 * @{
 * @struct iio_block
 * @brief A block of memory containing data samples */


/** @brief Create a data block for the given buffer
 * @param buf A pointer to an iio_buffer structure
 * @param size The size of the block to create, in bytes
 * @return On success, a pointer to an iio_block structure
 * @return On failure, a pointer-encoded error is returned */
__api __check_ret struct iio_block *
iio_buffer_create_block(struct iio_buffer *buffer, size_t size);


/** @brief Destroy the given block
 * @param block A pointer to an iio_block structure */
__api void iio_block_destroy(struct iio_block *block);


/** @brief Get the start address of the block
 * @param buf A pointer to an iio_block structure
 * @return A pointer corresponding to the start address of the block */
__api void *iio_block_start(const struct iio_block *block);


/** @brief Find the first sample of a channel in a block
 * @param buf A pointer to an iio_block structure
 * @param chn A pointer to an iio_channel structure
 * @return A pointer to the first sample found, or to the end of the block if
 * no sample for the given channel is present in the block
 *
 * <b>NOTE:</b> This function, coupled with iio_block_end, can be used to
 * iterate on all the samples of a given channel present in the block, doing
 * the following:
 *
 * @verbatim
 for (void *ptr = iio_block_first(block, chn);
      ptr < iio_block_end(block);
      ptr += iio_device_get_sample_size(dev, mask)) {
    ....
 }
 @endverbatim

 The iio_channel passed as argument must be from the iio_device that was used to
 create the iio_buffer and then the iio_block, otherwise the result is
 undefined. */
__api void *iio_block_first(const struct iio_block *block,
			    const struct iio_channel *chn);


/** @brief Get the address after the last sample in a block
 * @param block A pointer to an iio_block structure
 * @return A pointer corresponding to the address that follows the last sample
 * present in the buffer */
__api void *iio_block_end(const struct iio_block *block);


/** @brief Call the supplied callback for each sample found in a block
 * @param block A pointer to an iio_block structure
 * @param callback A pointer to a function to call for each sample found
 * @param data A user-specified pointer that will be passed to the callback
 * @return number of bytes processed.
 *
 * <b>NOTE:</b> The callback receives four arguments:
 * * A pointer to the iio_channel structure corresponding to the sample,
 * * A pointer to the sample itself,
 * * The length of the sample in bytes,
 * * The user-specified pointer passed to iio_block_foreach_sample. */
__api __check_ret ssize_t
iio_block_foreach_sample(const struct iio_block *block,
			 const struct iio_channels_mask *mask,
			 ssize_t (*callback)(const struct iio_channel *chn,
					     void *src, size_t bytes, void *d),
			 void *data);


/** @brief Enqueue the given iio_block to the buffer's queue
 * @param block A pointer to an iio_block structure
 * @param bytes_used The amount of data in bytes to be transferred (either
 * transmitted or received).
 * @param cyclic If True, enable cyclic mode. The block's content will be
 * repeated on the hardware's output until the buffer is cancelled or destroyed.
 * @return On success, 0 is returned
 * @return On error, a negative error code is returned
 *
 * <b>NOTE:</b> After iio_block_enqueue is called, the block's data must not be
 * accessed until iio_block_dequeue successfully returns. */
__api int iio_block_enqueue(struct iio_block *block, size_t bytes_used, bool cyclic);


/** @brief Dequeue the given iio_block from the buffer's queue
 * @param block A pointer to an iio_block structure
 * @param nonblock if True, the operation won't block and return -EBUSY if
 *   the block is not ready for dequeue.
 * @return On success, 0 is returned
 * @return On error, a negative error code is returned */
__api int iio_block_dequeue(struct iio_block *block, bool nonblock);


/** @brief Retrieve a pointer to the iio_buffer structure
 * @param block A pointer to an iio_block structure
 * @return A pointer to an iio_buffer structure */
__api struct iio_buffer * iio_block_get_buffer(const struct iio_block *block);


/** @} *//* ------------------------------------------------------------------*/
/* ------------------------- Stream functions --------------------------------*/
/** @defgroup Stream Stream
 * @{
 * @struct iio_stream
 * @brief A helper object to simplify reading/writing to a iio_buffer */


/** @brief Create a iio_stream object for the given iio_buffer
 * @param buffer A pointer to an iio_buffer structure
 * @param nb_blocks The number of iio_block objects to create, internally.
 *   In doubt, a good value is 4.
 * @param samples_count The size of the iio_block objects, in samples
 * @return On success, a pointer to an iio_stream structure
 * @return On failure, a pointer-encoded error is returned */
__api __check_ret struct iio_stream *
iio_buffer_create_stream(struct iio_buffer *buffer, size_t nb_blocks,
			 size_t samples_count);


/** @brief Destroy the given stream object
 * @param stream A pointer to an iio_stream structure */
__api void
iio_stream_destroy(struct iio_stream *stream);


/** @brief Get a pointer to the next data block
 * @param stream A pointer to an iio_stream structure
 * @return On success, a pointer to an iio_block structure
 * @return On failure, a pointer-encoded error is returned */
__api __check_ret const struct iio_block *
iio_stream_get_next_block(struct iio_stream *stream);


/** @} *//* ------------------------------------------------------------------*/
/* ---------------------------- HWMON support --------------------------------*/
/** @defgroup Hwmon Compatibility with hardware monitoring (hwmon) devices
 * @{
 * @enum hwmon_chan_type
 * @brief Hwmon channel type
 *
 * Libiio support hardware-monitoring (hwmon) devices as well. This enum
 * specifies the type of data associated with the hwmon channel.
 *
 * NOTE: as of 2021 only the current hwmon API is supported. The old
 * and deprecated APIs are not supported, and won't be supported unless we
 * have a case where updating a hwmon driver is not possible.
 */
enum hwmon_chan_type {
	HWMON_VOLTAGE,
	HWMON_FAN,
	HWMON_PWM,
	HWMON_TEMP,
	HWMON_CURRENT,
	HWMON_POWER,
	HWMON_ENERGY,
	HWMON_HUMIDITY,
	HWMON_INTRUSION,
	HWMON_CHAN_TYPE_UNKNOWN = IIO_CHAN_TYPE_UNKNOWN,
};

/**
 * @brief Get the type of the given hwmon channel
 * @param chn A pointer to an iio_channel structure
 * @return The type of the hwmon channel */
static inline enum hwmon_chan_type
hwmon_channel_get_type(const struct iio_channel *chn)
{
	return (enum hwmon_chan_type) iio_channel_get_type(chn);
}

/**
 * @brief Get whether or not the device is a hardware monitoring device
 * @param dev A pointer to an iio_device structure
 * @return True if the device is a hardware monitoring device,
 * false if it is a IIO device */
static inline bool iio_device_is_hwmon(const struct iio_device *dev)
{
	const char *id = iio_device_get_id(dev);

	return id[0] == 'h';
}


/** @} *//* ------------------------------------------------------------------*/
/* ------------------------- Low-level functions -----------------------------*/
/** @defgroup Debug Debug and low-level functions
 * @{
 * @struct iio_data_format
 * @brief Contains the format of a data sample.
 *
 * The different fields inform about the correct way to convert one sample from
 * its raw format (as read from / generated by the hardware) to its real-world
 * value.
 */
struct iio_data_format {
	/** @brief Total length of the sample, in bits */
	unsigned int length;

	/** @brief Length of valuable data in the sample, in bits */
	unsigned int bits;

	/** @brief Right-shift to apply when converting sample */
	unsigned int shift;

	/** @brief Contains True if the sample is signed */
	bool is_signed;

	/** @brief Contains True if the sample is fully defined, sign extended, etc. */
	bool is_fully_defined;

	/** @brief Contains True if the sample is in big-endian format */
	bool is_be;

	/** @brief Contains True if the sample should be scaled when converted */
	bool with_scale;

	/** @brief Contains the scale to apply if with_scale is set */
	double scale;

	/** @brief Number of times length repeats (added in v0.8) */
	unsigned int repeat;
};


/** @brief Create a new empty channels mask
 * @param nb_channels The number of channels in the mask
 * @return On success, a pointer to an iio_channels_mask structure
 * @return On error, NULL is returned */
__api struct iio_channels_mask *
iio_create_channels_mask(unsigned int nb_channels);


/** @brief Destroy a channels mask
 * @param mask A pointer to an iio_channels_mask structure */
__api void
iio_channels_mask_destroy(struct iio_channels_mask *mask);


/** @brief Get the current sample size
 * @param dev A pointer to an iio_device structure
 * @param mask A pointer to an iio_channels_mask structure. If NULL, the current
 *   channel mask of the iio_device object is used.
 * @return On success, the sample size in bytes
 * @return On error, a negative errno code is returned
 *
 * <b>NOTE:</b> The sample size is not constant and will change when channels
 * get enabled or disabled. */
__api __check_ret ssize_t
iio_device_get_sample_size(const struct iio_device *dev,
			   const struct iio_channels_mask *mask);


/** @brief Get the index of the given channel
 * @param chn A pointer to an iio_channel structure
 * @return On success, the index of the specified channel
 * @return On error, a negative errno code is returned */
__api __check_ret __pure long iio_channel_get_index(const struct iio_channel *chn);


/** @brief Get a pointer to a channel's data format structure
 * @param chn A pointer to an iio_channel structure
 * @return A pointer to the channel's iio_data_format structure */
__api __check_ret __cnst const struct iio_data_format * iio_channel_get_data_format(
		const struct iio_channel *chn);


/** @brief Convert the sample from hardware format to host format
 * @param chn A pointer to an iio_channel structure
 * @param dst A pointer to the destination buffer where the converted sample
 * should be written
 * @param src A pointer to the source buffer containing the sample */
__api void iio_channel_convert(const struct iio_channel *chn,
		void *dst, const void *src);


/** @brief Convert the sample from host format to hardware format
 * @param chn A pointer to an iio_channel structure
 * @param dst A pointer to the destination buffer where the converted sample
 * should be written
 * @param src A pointer to the source buffer containing the sample */
__api void iio_channel_convert_inverse(const struct iio_channel *chn,
		void *dst, const void *src);


/** @brief Enumerate the debug attributes of the given device
 * @param dev A pointer to an iio_device structure
 * @return The number of debug attributes found */
__api __check_ret __pure unsigned int iio_device_get_debug_attrs_count(
		const struct iio_device *dev);


/** @brief Get the debug attribute present at the given index
 * @param dev A pointer to an iio_device structure
 * @param index The index corresponding to the debug attribute
 * @return On success, a pointer to a static NULL-terminated string
 * @return If the index is invalid, NULL is returned */
__api __check_ret __pure const char * iio_device_get_debug_attr(
		const struct iio_device *dev, unsigned int index);


/** @brief Try to find a debug attribute by its name
 * @param dev A pointer to an iio_device structure
 * @param name A NULL-terminated string corresponding to the name of the
 * debug attribute
 * @return On success, a pointer to a static NULL-terminated string
 * @return If the name does not correspond to any known debug attribute of the
 * given device, NULL is returned
 *
 * <b>NOTE:</b> This function is useful to detect the presence of a debug
 * attribute.
 * It can also be used to retrieve the name of a debug attribute as a pointer
 * to a static string from a dynamically allocated string. */
__api __check_ret __pure const char * iio_device_find_debug_attr(
		const struct iio_device *dev, const char *name);


/** @brief Read the content of the given debug attribute
 * @param dev A pointer to an iio_device structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * debug attribute
 * @param dst A pointer to the memory area where the NULL-terminated string
 * corresponding to the value read will be stored
 * @param len The available length of the memory area, in bytes
 * @return On success, the number of bytes written to the buffer
 * @return On error, a negative errno code is returned */
__api __check_ret ssize_t
iio_device_debug_attr_read_raw(const struct iio_device *dev,
			       const char *attr, char *dst, size_t len);


/** @brief Read the content of the given debug attribute
 * @param dev A pointer to an iio_device structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * debug attribute
 * @param ptr A pointer to a variable where the value should be stored
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
#define iio_device_debug_attr_read(dev, attr, ptr)			\
	_Generic((ptr),							\
		 bool *: iio_device_debug_attr_read_bool,		\
		 long long *: iio_device_debug_attr_read_longlong,	\
		 double *: iio_device_debug_attr_read_double)(dev, attr, ptr)


/** @brief Set the value of the given debug attribute
 * @param dev A pointer to an iio_device structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * debug attribute
 * @param src A pointer to the data to be written
 * @param len The number of bytes that should be written
 * @return On success, the number of bytes written
 * @return On error, a negative errno code is returned */
__api __check_ret ssize_t
iio_device_debug_attr_write_raw(const struct iio_device *dev,
				const char *attr, const void *src, size_t len);


/** @brief Set the value of the given debug attribute
 * @param dev A pointer to an iio_device structure
 * @param attr A NULL-terminated string corresponding to the name of the
 * debug attribute
 * @param val A double value to set the debug attribute to
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
#define iio_device_debug_attr_write(dev, attr, val)			\
	_Generic((val),							\
		 const char *: iio_device_debug_attr_write_string,	\
		 char *: iio_device_debug_attr_write_string,		\
		 bool: iio_device_debug_attr_write_bool,		\
		 long long: iio_device_debug_attr_write_longlong,	\
		 double: iio_device_debug_attr_write_double)(dev, attr, val)


/** @brief Identify the channel or debug attribute corresponding to a filename
 * @param dev A pointer to an iio_device structure
 * @param filename A NULL-terminated string corresponding to the filename
 * @param chn A pointer to a pointer of an iio_channel structure. The pointed
 * pointer will be set to the address of the iio_channel structure if the
 * filename correspond to the attribute of a channel, or NULL otherwise.
 * @param attr A pointer to a NULL-terminated string. The pointer
 * pointer will be set to point to the name of the attribute corresponding to
 * the filename.
 * @return On success, 0 is returned, and *chn and *attr are modified.
 * @return On error, a negative errno code is returned. *chn and *attr are not
 * modified. */
__api __check_ret int iio_device_identify_filename(const struct iio_device *dev,
		const char *filename, struct iio_channel **chn,
		const char **attr);


/** @brief Set the value of a hardware register
 * @param dev A pointer to an iio_device structure
 * @param address The address of the register
 * @param value The value to set the register to
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
__api __check_ret int iio_device_reg_write(struct iio_device *dev,
		uint32_t address, uint32_t value);


/** @brief Get the value of a hardware register
 * @param dev A pointer to an iio_device structure
 * @param address The address of the register
 * @param value A pointer to the variable where the value will be written
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
__api __check_ret int iio_device_reg_read(struct iio_device *dev,
		uint32_t address, uint32_t *value);


/** @} */

#ifndef DOXYGEN
/* These functions can be used directly, but should be used through the generic
 * macros iio_{device,channel,device_buffer,device_debug}_attr_{read,write}() */
__api __check_ret int
iio_device_attr_read_bool(const struct iio_device *dev,
			  const char *attr, bool *val);
__api __check_ret int
iio_device_attr_read_longlong(const struct iio_device *dev,
			      const char *attr, long long *val);
__api __check_ret int
iio_device_attr_read_double(const struct iio_device *dev,
			    const char *attr, double *val);
__api __check_ret ssize_t
iio_device_attr_write_string(const struct iio_device *dev,
			     const char *attr, const char *src);
__api __check_ret int
iio_device_attr_write_bool(const struct iio_device *dev,
			   const char *attr, bool val);
__api __check_ret int
iio_device_attr_write_longlong(const struct iio_device *dev,
			       const char *attr, long long val);
__api __check_ret int
iio_device_attr_write_double(const struct iio_device *dev,
			     const char *attr, double val);
__api __check_ret int
iio_device_buffer_attr_read_bool(const struct iio_device *dev,
				 unsigned int buf_id,
				 const char *attr, bool *val);
__api __check_ret int
iio_device_buffer_attr_read_longlong(const struct iio_device *dev,
				     unsigned int buf_id,
				     const char *attr, long long *val);
__api __check_ret int
iio_device_buffer_attr_read_double(const struct iio_device *dev,
				   unsigned int buf_id,
				   const char *attr, double *val);
__api __check_ret ssize_t
iio_device_buffer_attr_write_string(const struct iio_device *dev,
				    unsigned int buf_id,
				    const char *attr, const char *src);
__api __check_ret int
iio_device_buffer_attr_write_bool(const struct iio_device *dev,
				  unsigned int buf_id,
				  const char *attr, bool val);
__api __check_ret int
iio_device_buffer_attr_write_longlong(const struct iio_device *dev,
				      unsigned int buf_id,
				      const char *attr, long long val);
__api __check_ret int
iio_device_buffer_attr_write_double(const struct iio_device *dev,
				    unsigned int buf_id,
				    const char *attr, double val);
__api __check_ret int
iio_channel_attr_read_bool(const struct iio_channel *chn,
			   const char *attr, bool *val);
__api __check_ret int
iio_channel_attr_read_longlong(const struct iio_channel *chn,
			       const char *attr, long long *val);
__api __check_ret int
iio_channel_attr_read_double(const struct iio_channel *chn,
			     const char *attr, double *val);
__api __check_ret ssize_t
iio_channel_attr_write_string(const struct iio_channel *chn,
			      const char *attr, const char *src);
__api __check_ret
int iio_channel_attr_write_bool(const struct iio_channel *chn,
				const char *attr, bool val);
__api __check_ret int
iio_channel_attr_write_longlong(const struct iio_channel *chn,
				const char *attr, long long val);
__api __check_ret int
iio_channel_attr_write_double(const struct iio_channel *chn,
			      const char *attr, double val);
__api __check_ret int
iio_device_debug_attr_read_bool(const struct iio_device *dev,
				const char *attr, bool *val);
__api __check_ret int
iio_device_debug_attr_read_longlong(const struct iio_device *dev,
				    const char *attr, long long *val);
__api __check_ret int
iio_device_debug_attr_read_double(const struct iio_device *dev,
				  const char *attr, double *val);
__api __check_ret ssize_t
iio_device_debug_attr_write_string(const struct iio_device *dev,
				   const char *attr, const char *src);
__api __check_ret int
iio_device_debug_attr_write_bool(const struct iio_device *dev,
				 const char *attr, bool val);
__api __check_ret int
iio_device_debug_attr_write_longlong(const struct iio_device *dev,
				     const char *attr, long long val);
__api __check_ret int
iio_device_debug_attr_write_double(const struct iio_device *dev,
				   const char *attr, double val);
#endif /* DOXYGEN */

#ifdef __cplusplus
}
#endif

#undef __api

#endif /* __IIO_H__ */
