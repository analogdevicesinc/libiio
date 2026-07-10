#ifndef __VITA49_2_IIOD_HELPERS_H__
#define __VITA49_2_IIOD_HELPERS_H__

#include <iio/iio.h>
#include <vita49_2/vita49_2_packet_elements.h>

#include <errno.h>
#include <stdint.h>

/**
 * @brief Maps Linux errnos to VITA 49.2 warning/error indicators. See Table 8.4.1.2.1-1 in the VITA 49.2 2017 document.
 * 
 * VITA 49.2 Warnings/Errors:
 * 
 * 31 = Field NOT Executed
 * 
 * 30 = Device Failure
 * 
 * 29 = Erroneous Field
 * 
 * 28 = Parameter Out-of-Range
 * 
 * 27 = Parameter Unsupported Precision
 * 
 * 26 = Field Value Invalid
 * 
 * 25 = Timestamp Problem
 * 
 * 24 = Hazardous Power Levels
 * 
 * 23 = Distortion
 * 
 * 22 = In-Band Power Compliance
 * 
 * 21 = Out-of-Band Power Compliance
 * 
 * 20 = Co-Site Interference
 * 
 * 19 = Regional Interference
 * 
 * 18-13 = Reserved
 * 
 * 12-1 = User Defined
 * 
 * 0 = Reserved
 * 
 * See the vita49_2_warnings_error_codes enum.
 */
static const uint8_t VITA49_2_ERRNO_MAP[] = {
    [0]                 = ENONE,
    [EPERM]             = ENOEXECUTE,   /* Operation not permitted */
    [ENOENT]            = ENOFIELD,     /* No such file/directory -> erroneous field */
    [ESRCH]             = ENOFIELD,
    [EINTR]             = ENOEXECUTE,   /* Interrupted -> not executed */
    [EIO]               = EDEVFAIL,     /* I/O error -> device failure */
    [ENXIO]             = EDEVFAIL,     /* No such device or address */
    [E2BIG]             = EOUTRANGE,    /* Argument list too long -> out-of-range */
    [ENOEXEC]           = ENOFIELD,     /* Exec format error */
    [EBADF]             = ENOEXECUTE,
    [ECHILD]            = ENOEXECUTE,
    [EAGAIN]            = ENOEXECUTE,   /* Try again */
    [ENOMEM]            = EDEVFAIL,     /* Out of memory */
    [EACCES]            = ENOEXECUTE,   /* Permission denied */
    [EFAULT]            = EBADARGS,     /* Bad address -> bad arguments */
    [ENOTBLK]           = ENOFIELD,
    [EBUSY]             = ENOEXECUTE,   /* Device busy */
    [EEXIST]            = ENOFIELD,
    [EXDEV]             = ENOFIELD,
    [ENODEV]            = EDEVFAIL,     /* No such device */
    [ENOTDIR]           = ENOFIELD,
    [EISDIR]            = ENOFIELD,
    [EINVAL]            = EINVALID,     /* Invalid argument */
    [ENFILE]            = EDEVFAIL,
    [EMFILE]            = EDEVFAIL,
    [ENOTTY]            = ENOFIELD,
    [ETXTBSY]           = ENOEXECUTE,
    [EFBIG]             = EOUTRANGE,    /* File too large -> out-of-range */
    [ENOSPC]            = EDEVFAIL,     /* No space left */
    [ESPIPE]            = ENOFIELD,
    [EROFS]             = ENOEXECUTE,   /* Read-only */
    [EMLINK]            = ENOFIELD,
    [EPIPE]             = EDEVFAIL,     /* Broken pipe */
    [EDOM]              = EOUTRANGE,    /* Math domain error */
    [ERANGE]            = EOUTRANGE,    /* Result not representable */
    [EDEADLK]           = ENOEXECUTE,
    [ENAMETOOLONG]      = ENOFIELD,
    [ENOLCK]            = EDEVFAIL,
    [ENOSYS]            = ENOFIELD,     /* Function not implemented */
    [ENOTEMPTY]         = ENOFIELD,
    [ELOOP]             = ENOFIELD,
    [41]                = ENOEXECUTE,   /* unused gap */
    [ENOMSG]            = ENOEXECUTE,
    [EIDRM]             = ENOEXECUTE,
    [ECHRNG]            = EOUTRANGE,    /* Channel number out of range */
    [EL2NSYNC]          = EDEVFAIL,
    [EL3HLT]            = EDEVFAIL,
    [EL3RST]            = EDEVFAIL,
    [ELNRNG]            = EOUTRANGE,
    [EUNATCH]           = EDEVFAIL,
    [ENOCSI]            = EDEVFAIL,
    [EL2HLT]            = EDEVFAIL,
    [EBADE]             = EINVALID,     /* Invalid exchange */
    [EBADR]             = ENOFIELD,     /* Invalid request descriptor */
    [EXFULL]            = EOUTRANGE,
    [ENOANO]            = EDEVFAIL,
    [EBADRQC]           = ENOFIELD,     /* Invalid request code */
    [EBADSLT]           = ENOFIELD,     /* Invalid slot */
    [58]                = ENOEXECUTE,   /* unused gap */
    [EBFONT]            = ENOFIELD,
    [ENOSTR]            = ENOFIELD,
    [ENODATA]           = ENOEXECUTE,
    [ETIME]             = ETIMESTAMP,   /* Timer expired -> timestamp problem */
    [ENOSR]             = EDEVFAIL,
    [ENONET]            = ENOEXECUTE,
    [ENOPKG]            = ENOFIELD,
    [EREMOTE]           = EDEVFAIL,
    [ENOLINK]           = EDEVFAIL,
    [EADV]              = ENOEXECUTE,
    [ESRMNT]            = ENOEXECUTE,
    [ECOMM]             = EDEVFAIL,
    [EPROTO]            = EDEVFAIL,
    [EMULTIHOP]         = EDEVFAIL,
    [EDOTDOT]           = EDEVFAIL,
    [EBADMSG]           = ENOFIELD,
    [EOVERFLOW]         = EOUTRANGE,    /* Value too large */
    [ENOTUNIQ]          = ENOFIELD,
    [EBADFD]            = ENOEXECUTE,
    [EREMCHG]           = EDEVFAIL,
    [ELIBACC]           = EDEVFAIL,
    [ELIBBAD]           = EDEVFAIL,
    [ELIBSCN]           = EDEVFAIL,
    [ELIBMAX]           = EDEVFAIL,
    [ELIBEXEC]          = EDEVFAIL,
    [EILSEQ]            = EINVALID,     /* Illegal byte sequence */
    [ERESTART]          = ENOEXECUTE,
    [ESTRPIPE]          = EDEVFAIL,
    [EUSERS]            = ENOEXECUTE,
    [ENOTSOCK]          = ENOFIELD,
    [EDESTADDRREQ]      = ENOFIELD,
    [EMSGSIZE]          = EOUTRANGE,    /* Message too long */
    [EPROTOTYPE]        = ENOFIELD,
    [ENOPROTOOPT]       = ENOFIELD,
    [EPROTONOSUPPORT]   = ENOFIELD,
    [ESOCKTNOSUPPORT]   = ENOFIELD,
    [EOPNOTSUPP]        = ENOFIELD,     /* Operation not supported */
    [EPFNOSUPPORT]      = ENOFIELD,
    [EAFNOSUPPORT]      = ENOFIELD,
    [EADDRINUSE]        = ENOEXECUTE,
    [EADDRNOTAVAIL]     = ENOFIELD,
    [ENETDOWN]          = EDEVFAIL,
    [ENETUNREACH]       = EDEVFAIL,
    [ENETRESET]         = EDEVFAIL,
    [ECONNABORTED]      = EDEVFAIL,
    [ECONNRESET]        = EDEVFAIL,
    [ENOBUFS]           = EDEVFAIL,
    [EISCONN]           = ENOEXECUTE,
    [ENOTCONN]          = EDEVFAIL,
    [ESHUTDOWN]         = ENOEXECUTE,
    [ETOOMANYREFS]      = EDEVFAIL,
    [ETIMEDOUT]         = ENOEXECUTE,   /* Connection timed out */
    [ECONNREFUSED]      = EDEVFAIL,
    [EHOSTDOWN]         = EDEVFAIL,
    [EHOSTUNREACH]      = EDEVFAIL,
    [EALREADY]          = ENOEXECUTE,
    [EINPROGRESS]       = ENOEXECUTE,
    [ESTALE]            = EDEVFAIL,
    [EUCLEAN]           = EDEVFAIL,
    [ENOTNAM]           = ENOFIELD,
    [ENAVAIL]           = EDEVFAIL,
    [EISNAM]            = ENOFIELD,
    [EREMOTEIO]         = EDEVFAIL,     /* Remote I/O error */
    [EDQUOT]            = EOUTRANGE,    /* Quota exceeded */
    [ENOMEDIUM]         = EDEVFAIL,
    [EMEDIUMTYPE]       = ENOFIELD,
    [ECANCELED]         = ENOEXECUTE,   /* Cancelled */
    [ENOKEY]            = ENOEXECUTE,
    [EKEYEXPIRED]       = ENOEXECUTE,
    [EKEYREVOKED]       = ENOEXECUTE,
    [EKEYREJECTED]      = ENOEXECUTE,
    [EOWNERDEAD]        = EDEVFAIL,
    [ENOTRECOVERABLE]   = EDEVFAIL,
    [ERFKILL]           = EHAZPOWER,    /* RF-kill -> hazardous power */
};

enum vita49_2_warnings_error_codes get_available_attribute_values(struct iio_context *ctx, const char* const device_name, const char* const channel_name, const char* const attribute_name, bool is_output, char* available_range, size_t buffer_size);

enum vita49_2_warnings_error_codes validate_command_ll(struct iio_context *ctx, const char* const device_name, const char* const channel_name, const char* const attribute_name, bool is_output, long long new_value);

enum vita49_2_warnings_error_codes validate_command_double_h(struct iio_context *ctx, const char* const device_name, const char* const channel_name, const char* const attribute_name, bool is_output, double new_value);

enum vita49_2_warnings_error_codes validate_command_s(struct iio_context *ctx, const char* const device_name, const char* const channel_name, const char* const attribute_name, bool is_output, const char* const new_value);

/**
 * @brief Retrieves a handle to an IIO device (high level wrapper around libiio calls).
 * 
 * @param ctx
 * @param device_name
 * @param channel_name
 * @param attribute_name
 * @param is_output
 * @param attribute Provide a double pointer to an attribute struct which will be updated by this function if the attribute exists.
 * @return enum vita49_2_warnings_error_codes
 */
enum vita49_2_warnings_error_codes vita49_2_find_iio_attribute(const struct iio_context* const ctx, const char* const device_name, const char* const channel_name, const char* const attribute_name, bool is_output, const struct iio_attr** attribute);

/**
 * @brief Validates a specific command that takes a uint32_t attribute value.
 * 
 * Returns a negative value if there's an error, otherwise 0 is returned.
 * 
 * @param ctx 
 * @param cif_type Which CIF group (0, 1, 3, 7).
 * @param cif_bit 0 to 31.
 * @param new_value The new value that you want to validate before assigning to the attribute.
 * @return enum vita49_2_warnings_error_codes 
 */
enum vita49_2_warnings_error_codes validate_command_u32(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, uint32_t new_value);

/**
 * @brief Validates a specific command that takes a uint64_t attribute value.
 * 
 * Returns a negative value if there's an error, otherwise 0 is returned.
 * 
 * @param ctx 
 * @param cif_type Which CIF group (0, 1, 3, 7).
 * @param cif_bit 0 to 31.
 * @param new_value The new value that you want to validate before assigning to the attribute.
 * @return enum vita49_2_warnings_error_codes 
 */
enum vita49_2_warnings_error_codes validate_command_u64(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, uint64_t new_value);

/**
 * @brief Validates a specific command that takes a int64_t attribute value.
 * 
 * Returns a negative value if there's an error, otherwise 0 is returned.
 * 
 * @param ctx 
 * @param cif_type Which CIF group (0, 1, 3, 7).
 * @param cif_bit 0 to 31.
 * @param new_value The new value that you want to validate before assigning to the attribute.
 * @return enum vita49_2_warnings_error_codes 
 */
enum vita49_2_warnings_error_codes validate_command_i64(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, int64_t new_value);

/**
 * @brief Validates a specific command that takes a float attribute value.
 * 
 * Returns a negative value if there's an error, otherwise 0 is returned.
 * 
 * @param ctx 
 * @param cif_type Which CIF group (0, 1, 3, 7).
 * @param cif_bit 0 to 31.
 * @param new_value The new value that you want to validate before assigning to the attribute.
 * @return enum vita49_2_warnings_error_codes 
 */
enum vita49_2_warnings_error_codes validate_command_float(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, float new_value);

/**
 * @brief Validates a specific command that takes a double attribute value.
 * 
 * Returns a negative value if there's an error, otherwise 0 is returned.
 * 
 * @param ctx 
 * @param cif_type Which CIF group (0, 1, 3, 7).
 * @param cif_bit 0 to 31.
 * @param new_value The new value that you want to validate before assigning to the attribute.
 * @return enum vita49_2_warnings_error_codes 
 */
enum vita49_2_warnings_error_codes validate_command_double(struct iio_context *ctx, uint8_t cif_type, uint8_t cif_bit, double new_value);

#endif /* __VITA49_2_IIOD_HELPERS_H__ */
