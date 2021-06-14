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

#include <iio.h>

#ifndef DOXYGEN
#ifdef LIBIIO_COMPAT_EXPORTS
#define __api __iio_api_export
#else
#define __api __iio_api_import
#endif
#endif


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


/** @} *//* ------------------------------------------------------------------*/

#undef __api

#endif /* __LIBIIO_COMPAT_H__ */
