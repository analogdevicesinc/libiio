#include "iiod-client.h"
#include "iio-lock.h"

#include <errno.h>
#include <string.h>

struct iiod_client {
	struct iio_context_pdata *pdata;
	const struct iiod_client_ops *ops;
	struct iio_mutex *lock;
};

struct iiod_client * iiod_client_new(struct iio_context_pdata *pdata,
		struct iio_mutex *lock, const struct iiod_client_ops *ops)
{
	struct iiod_client *client;

	client = malloc(sizeof(*client));
	if (!client) {
		errno = ENOMEM;
		return NULL;
	}

	client->lock = lock;
	client->pdata = pdata;
	client->ops = ops;
	return client;
}

void iiod_client_destroy(struct iiod_client *client)
{
	free(client);
}

int iiod_client_get_version(struct iiod_client *client, int desc,
		unsigned int *major, unsigned int *minor, char *git_tag)
{
	struct iio_context_pdata *pdata = client->pdata;
	const struct iiod_client_ops *ops = client->ops;
	char buf[256], *ptr = buf, *end;
	long maj, min;
	int ret;

	iio_mutex_lock(client->lock);

	ret = ops->write(pdata, desc, "VERSION\r\n", sizeof("VERSION\r\n") - 1);
	if (ret < 0) {
		iio_mutex_unlock(client->lock);
		return ret;
	}

	ret = ops->read(pdata, desc, buf, sizeof(buf));
	iio_mutex_unlock(client->lock);

	if (ret < 0)
		return ret;

	maj = strtol(ptr, &end, 10);
	if (ptr == end)
		return -EIO;

	ptr = end + 1;
	min = strtol(ptr, &end, 10);
	if (ptr == end)
		return -EIO;

	ptr = end + 1;
	if (buf + ret < ptr + 8)
		return -EIO;

	/* Strip the \n */
	ptr[buf + ret - ptr - 1] = '\0';

	if (major)
		*major = (unsigned int) maj;
	if (minor)
		*minor = (unsigned int) min;
	if (git_tag)
		strncpy(git_tag, ptr, 8);
	return 0;
}
