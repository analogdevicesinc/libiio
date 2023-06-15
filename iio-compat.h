/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Libiio 0.x to 1.x compat library
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */
/** @file iio-compat.h
 * @brief Libiio 0.x to 1.x compatibility library */

#ifndef __LIBIIO_COMPAT_H__
#define __LIBIIO_COMPAT_H__

#include <iio/iio.h>

#ifndef DOXYGEN
#ifdef LIBIIO_COMPAT_EXPORTS
#define __api __iio_api_export
#else
#define __api __iio_api_import
#endif
#endif

struct iio_context_info;
struct iio_scan_block;
struct iio_scan_context;


/* ---------------------------------------------------------------------------*/
/* ------------------------- Libiio 0.x to 1.x compat API --------------------*/
/** @defgroup TopLevel Top-level functions
 * @{ */


/** @brief Create a context from local or remote IIO devices
 * @return On success, A pointer to an iio_context structure
 * @return On failure, NULL is returned and errno is set appropriately
 *
 * <b>NOTE:</b> This function will create a context with the URI
 * provided in the IIOD_REMOTE environment variable. If not set, a local
 * context will be created instead. */
__api __check_ret struct iio_context * iio_create_default_context(void);


/** @brief Create a context from local IIO devices (Linux only)
 * @return On success, A pointer to an iio_context structure
 * @return On failure, NULL is returned and errno is set appropriately */
__api __check_ret struct iio_context * iio_create_local_context(void);


/** @brief Create a context from the network
 * @param host Hostname, IPv4 or IPv6 address where the IIO Daemon is running
 * @return On success, a pointer to an iio_context structure
 * @return On failure, NULL is returned and errno is set appropriately */
__api __check_ret struct iio_context * iio_create_network_context(const char *host);


/** @brief Create a context from a XML file
 * @param xml_file Path to the XML file to open
 * @return On success, A pointer to an iio_context structure
 * @return On failure, NULL is returned and errno is set appropriately
 *
 * <b>NOTE:</b> The format of the XML must comply to the one returned by
 * iio_context_get_xml. */
__api __check_ret struct iio_context * iio_create_xml_context(const char *xml_file);


/** @brief Create a context from a URI description
 * @param uri A URI describing the context location
 * @return On success, a pointer to a iio_context structure
 * @return On failure, NULL is returned and errno is set appropriately
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
 *   <b>or</b> <i>"ip:plutosdr.local"</i>
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
__api __check_ret struct iio_context * iio_create_context_from_uri(const char *uri);


/** @brief Create a scan context
 * @param backend A NULL-terminated string containing the backend(s) to use for
 * scanning (example: pre version 0.20 :  "local", "ip", or "usb"; post version
 * 0.20 can handle multiple, including "local:usb:", "ip:usb:", "local:usb:ip:").
 * If NULL, all the available backends are used.
 * @param flags Unused for now. Set to 0.
 * @return on success, a pointer to a iio_scan_context structure
 * @return On failure, NULL is returned and errno is set appropriately */
__api __check_ret struct iio_scan_context *
iio_create_scan_context(const char *backend, unsigned int flags);


/** @brief Destroy the given scan context
 * @param ctx A pointer to an iio_scan_context structure
 *
 * <b>NOTE:</b> After that function, the iio_scan_context pointer shall be invalid. */
__api void iio_scan_context_destroy(struct iio_scan_context *ctx);


/** @brief Enumerate available contexts
 * @param ctx A pointer to an iio_scan_context structure
 * @param info A pointer to a 'const struct iio_context_info **' typed variable.
 * The pointed variable will be initialized on success.
 * @returns On success, the number of contexts found.
 * @returns On failure, a negative error number.
 */
__api __check_ret ssize_t
iio_scan_context_get_info_list(struct iio_scan_context *ctx,
			       struct iio_context_info ***info);


/** @brief Free a context info list
 * @param info A pointer to a 'const struct iio_context_info *' typed variable
 */
__api void iio_context_info_list_free(struct iio_context_info **info);


/** @brief Get a description of a discovered context
 * @param info A pointer to an iio_context_info structure
 * @return A pointer to a static NULL-terminated string
 */
__api __check_ret __pure const char *
iio_context_info_get_description(const struct iio_context_info *info);


/** @brief Get the URI of a discovered context
 * @param info A pointer to an iio_context_info structure
 * @return A pointer to a static NULL-terminated string
 */
__api __check_ret __pure const char *
iio_context_info_get_uri(const struct iio_context_info *info);


/** @brief Create a scan block
 * @param backend A NULL-terminated string containing the backend to use for
 * scanning. If NULL, all the available backends are used.
 * @param flags Unused for now. Set to 0.
 * @return on success, a pointer to a iio_scan_block structure
 * @return On failure, NULL is returned and errno is set appropriately
 *
 * Introduced in version 0.20. */
__api struct iio_scan_block *
iio_create_scan_block(const char *backend, unsigned int flags);


/** @brief Destroy the given scan block
 * @param blk A pointer to an iio_scan_block structure
 *
 * <b>NOTE:</b> After that function, the iio_scan_block pointer shall be invalid.
 *
 * Introduced in version 0.20. */
__api void iio_scan_block_destroy(struct iio_scan_block *blk);


/** @brief Enumerate available contexts via scan block
 * @param blk A pointer to a iio_scan_block structure.
 * @returns On success, the number of contexts found.
 * @returns On failure, a negative error number.
 *
 * Introduced in version 0.20. */
__api ssize_t iio_scan_block_scan(struct iio_scan_block *blk);


/** @brief Get the iio_context_info for a particular context
 * @param blk A pointer to an iio_scan_block structure
 * @param index The index corresponding to the context.
 * @return A pointer to the iio_context_info for the context
 * @returns On success, a pointer to the specified iio_context_info
 * @returns On failure, NULL is returned and errno is set appropriately
 *
 * Introduced in version 0.20. */
__api struct iio_context_info *
iio_scan_block_get_info(struct iio_scan_block *blk, unsigned int index);


/** @brief Get the version of the libiio library
 * @param major A pointer to an unsigned integer (NULL accepted)
 * @param minor A pointer to an unsigned integer (NULL accepted)
 * @param git_tag A pointer to a 8-characters buffer (NULL accepted) */
__api void iio_library_get_version(unsigned int *major,
		unsigned int *minor, char git_tag[8]);


/** @brief Get the version of the backend in use
 * @param ctx A pointer to an iio_context structure
 * @param major A pointer to an unsigned integer (NULL accepted)
 * @param minor A pointer to an unsigned integer (NULL accepted)
 * @param git_tag A pointer to a 8-characters buffer (NULL accepted)
 * @return On success, 0 is returned
 * @return On error, a negative errno code is returned */
__api __check_ret int iio_context_get_version(const struct iio_context *ctx,
					      unsigned int *major,
					      unsigned int *minor,
					      char git_tag[8]);


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

#undef __api

#endif /* __LIBIIO_COMPAT_H__ */
