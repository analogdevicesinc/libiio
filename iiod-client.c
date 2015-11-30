#include "iiod-client.h"
#include "iio-lock.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>

struct iiod_client {
	struct iio_context_pdata *pdata;
	const struct iiod_client_ops *ops;
	struct iio_mutex *lock;
};

static ssize_t iiod_client_read_integer(struct iiod_client *client,
		int desc, int *val)
{
	const struct iiod_client_ops *ops = client->ops;
	unsigned int i;
	char buf[1024], *ptr = NULL, *end;
	ssize_t ret;
	int value;

	do {
		ret = client->ops->read_line(client->pdata,
				desc, buf, sizeof(buf));
		if (ret < 0)
			return ret;

		for (i = 0; i < (unsigned int) ret; i++) {
			if (buf[i] != '\n') {
				if (!ptr)
					ptr = &buf[i];
			} else if (!!ptr) {
				break;
			}
		}
	} while (!ptr);

	buf[i] = '\0';

	value = (int) strtol(ptr, &end, 10);
	if (ptr == end)
		return -EINVAL;

	*val = value;
	return 0;
}

static int iiod_client_exec_command(struct iiod_client *client,
		int desc, const char *cmd)
{
	int resp;
	ssize_t ret;

	ret = client->ops->write(client->pdata, desc, cmd, strlen(cmd));
	if (ret < 0)
		return (int) ret;

	ret = iiod_client_read_integer(client, desc, &resp);
	return ret < 0 ? (int) ret : resp;
}

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

int iiod_client_get_trigger(struct iiod_client *client, int desc,
		const struct iio_device *dev, const struct iio_device **trigger)
{
	struct iio_context_pdata *pdata = client->pdata;
	const struct iio_context *ctx = iio_device_get_context(dev);
	unsigned int i, nb_devices = iio_context_get_devices_count(ctx);
	char buf[1024];
	unsigned int name_len;
	int ret;

	snprintf(buf, sizeof(buf), "GETTRIG %s\r\n", iio_device_get_id(dev));

	iio_mutex_lock(client->lock);
	ret = iiod_client_exec_command(client, desc, buf);

	if (ret == 0)
		*trigger = NULL;
	if (ret <= 0)
		goto out_unlock;

	if ((unsigned int) ret > sizeof(buf)) {
		ret = -EIO;
		goto out_unlock;
	}

	name_len = ret;

	ret = (int) client->ops->read(pdata, desc, buf, name_len);
	if (ret < 0)
		goto out_unlock;

	ret = -ENXIO;

	for (i = 0; i < nb_devices; i++) {
		struct iio_device *cur = iio_context_get_device(ctx, i);

		if (iio_device_is_trigger(cur)) {
			const char *name = iio_device_get_name(cur);

			if (!name)
				continue;

			if (!strncmp(name, buf, name_len)) {
				*trigger = cur;
				return 0;
			}
		}
	}

out_unlock:
	iio_mutex_unlock(client->lock);
	return ret;
}

int iiod_client_set_trigger(struct iiod_client *client, int desc,
		const struct iio_device *dev, const struct iio_device *trigger)
{
	const struct iiod_client_ops *ops = client->ops;
	char buf[1024];
	int ret;

	if (trigger)
		snprintf(buf, sizeof(buf), "SETTRIG %s %s\r\n",
				iio_device_get_id(dev),
				iio_device_get_id(trigger));
	else
		snprintf(buf, sizeof(buf), "SETTRIG %s\r\n",
				iio_device_get_id(dev));

	iio_mutex_lock(client->lock);
	ret = iiod_client_exec_command(client, desc, buf);
	iio_mutex_unlock(client->lock);
	return ret;
}
