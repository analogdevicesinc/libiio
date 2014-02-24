#include "../debug.h"
#include "../iio.h"

#include <string.h>

int main(int argc, char **argv)
{
	char *xml;
	struct iio_context *ctx;
	char *backend = getenv("LIBIIO_BACKEND");

	if (backend && !strcmp(backend, "xml")) {
		if (argc < 2) {
			ERROR("The XML backend requires the XML file to be "
					"passed as argument\n");
			return EXIT_FAILURE;
		}

		INFO("Creating XML IIO context\n");
		ctx = iio_create_xml_context(argv[1]);
	} else {
		INFO("Creating local IIO context\n");
		ctx = iio_create_local_context();
	}
	if (!ctx) {
		ERROR("Unable to create local context\n");
		return EXIT_FAILURE;
	}

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	INFO("XML generated:\n\n%s\n\n", xml);

	iio_context_destroy(ctx);

	ctx = iio_create_xml_context_mem(xml);
	if (!ctx) {
		ERROR("Unable to re-generate context\n");
	} else {
		INFO("Context re-creation from generated XML suceeded!\n");
		iio_context_destroy(ctx);
	}
	free(xml);
	return EXIT_SUCCESS;
}
