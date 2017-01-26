#ifndef MATLAB_LOADLIBRARY
#define MATLAB_LOADLIBRARY
#include <iio.h>

#ifndef __api
#define __api
#endif

struct iio_scan_block;

/** @brief Create a scan block
* @param backend A NULL-terminated string containing the backend to use for
* scanning. If NULL, all the available backends are used.
* @param flags Unused for now. Set to 0.
* @return on success, a pointer to a iio_scan_block structure
* @return On failure, NULL is returned and errno is set appropriately */
__api struct iio_scan_block * iio_create_scan_block(
		const char *backend, unsigned int flags);


/** @brief Destroy the given scan block
* @param ctx A pointer to an iio_scan_block structure
*
* <b>NOTE:</b> After that function, the iio_scan_block pointer shall be invalid. */
__api void iio_scan_block_destroy(struct iio_scan_block *blk);


/** @brief Enumerate available contexts via scan block
* @param blk A pointer to a iio_scan_block structure.
* @returns On success, the number of contexts found.
* @returns On failure, a negative error number.
*/
__api ssize_t iio_scan_block_scan(struct iio_scan_block *blk);


/** @brief Get the iio_context_info for a particular context
* @param blk A pointer to an iio_scan_block structure
* @param index The index corresponding to the context.
* @return A pointer to the iio_context_info for the context
* @returns On success, a pointer to the specified iio_context_info
* @returns On failure, NULL is returned and errno is set appropriately
*/
__api struct iio_context_info *iio_scan_block_get_info(
		struct iio_scan_block *blk, unsigned int index);

#endif
