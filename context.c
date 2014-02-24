#include "debug.h"
#include "iio-private.h"

#include <string.h>

static const char xml_header[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<!DOCTYPE context ["
"<!ELEMENT context (device)*>"
"<!ELEMENT device (channel | attribute)*>"
"<!ELEMENT channel (attribute)*>"
"<!ELEMENT attribute EMPTY>"
"<!ATTLIST context name CDATA #REQUIRED>"
"<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST channel id CDATA #REQUIRED type CDATA #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST attribute name CDATA #REQUIRED>"
"]>";

/* Returns a string containing the XML representation of this context */
char * iio_context_get_xml(const struct iio_context *ctx)
{
	size_t len = strlen(ctx->name) +
		sizeof(xml_header) - 1 +
		sizeof("<context name=\"\" ></context>");
	char *str, *ptr, *devices[ctx->nb_devices];
	size_t devices_len[ctx->nb_devices];
	unsigned int i;

	for (i = 0; i < ctx->nb_devices; i++) {
		char *xml = iio_device_get_xml(ctx->devices[i],
				&devices_len[i]);
		if (!xml)
			goto err_free_devices;
		devices[i] = xml;
		len += devices_len[i];
	}

	str = malloc(len);
	if (!str)
		goto err_free_devices;

	sprintf(str, "%s<context name=\"%s\" >", xml_header, ctx->name);
	ptr = strrchr(str, '\0');

	for (i = 0; i < ctx->nb_devices; i++) {
		strcpy(ptr, devices[i]);
		ptr += devices_len[i];
		free(devices[i]);
	}

	strcpy(ptr, "</context>");
	return str;

err_free_devices:
	while (i--)
		free(devices[i]);
	return NULL;
}

const char * iio_context_get_name(const struct iio_context *ctx)
{
	return ctx->name;
}

void iio_context_destroy(struct iio_context *ctx)
{
	unsigned int i;
	if (ctx->ops->shutdown)
		ctx->ops->shutdown(ctx);

	for (i = 0; i < ctx->nb_devices; i++)
		free_device(ctx->devices[i]);
	if (ctx->nb_devices)
		free(ctx->devices);
	free(ctx);
}

unsigned int iio_context_get_devices_count(const struct iio_context *ctx)
{
	return ctx->nb_devices;
}

struct iio_device * iio_context_get_device(const struct iio_context *ctx,
		unsigned int index)
{
	if (index >= ctx->nb_devices)
		return NULL;
	else
		return ctx->devices[index];
}
