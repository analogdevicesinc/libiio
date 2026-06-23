
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ini.h>

static void dump_if_nonprintable(char *name, const char *buf, size_t len)
{
	size_t i;
	const unsigned char * ch = (const unsigned char *)buf;
	bool bad = false;

	for (i = 0; i < len; i++) {

		if (ch[i] < 0x20 || ch[i] >= 0x7e) {
			bad = true;
			break;
		}
	}

	if (!bad)
		return;

	printf("\t%s (len=%zu) includes non-printable chars\n", name, len);

	for (i = 0; i < len; i += 16) {
		size_t j;

		/* offset */
		printf("\t\t%04zx : ", i);

		/* hex bytes */
		for (j = 0; j < 16; j++) {
			if (i + j < len) {
				if (ch[i + j] == '\t')
					printf("\\t  ");
				else
					printf("%02x ", ch[i + j]);
			} else
				printf("   ");

			if (j == 7)
				printf(" ");
		}

		printf(" |");

		/* ASCII */
		for (j = 0; j < 16 && i + j < len; j++) {
			unsigned char c = ch[i + j];
			if (c > 0x20 && c <= 0x7e)
				putchar(c);
			else
				putchar('.');
		}

		printf("|\n");
	}
}

int main(int argc, char **argv)
{
	struct INI *ini;
	char *key, *value;
	int line = 0;

	if (argc < 2) {
		printf("USAGE: test [INI_FILE]...\n");
		return EXIT_SUCCESS;
	}

	errno = 0;
	ini = ini_open(argv[1]);
	if (!ini) {
		fprintf(stderr, "\'%s\' INI file failed to open\nERROR : %s (%i)\n",
				argv[1], strerror(errno), -errno);
		return EXIT_FAILURE;
	}
	printf("\'%s\' INI file opened.\n", argv[1]);

	while (1) {
		const char *buf;
		char *name;
		size_t name_len;

		int res = ini_next_section(ini, &buf, &name_len);
		if (!res) {
			printf("[%02i] : No more sections.\n", line + 1);
			break;
		}
		if (res < 0) {
			printf("[%02i] : Section ERROR: %s (%i)\n", line + 1, strerror(-res), res);
			goto error;
		}

		name = malloc(name_len + 1);
		if (!name) goto error;
		name[name_len] = '\0';
		memcpy(name, buf, name_len);
		line = ini_get_line_number(ini, buf);
		printf("[%02i] : Opening section: \'%s\'\n",line, name);
		dump_if_nonprintable("section", name, name_len);
		free(name);

		while (1) {
			const char *buf2;
			size_t key_len, value_len;
			res = ini_read_pair(ini, &buf, &key_len, &buf2, &value_len);
			if (!res) {
				printf("[%02i] : No more key/value pairs.\n", line + 1);
				break;
			}
			if (res < 0) {
				printf("[%02i] : Key/Value ERROR: %s (%i)\n", line + 1, strerror(-res), res);
				goto error;
			}
			/* buf is only updated when ini_read_pair suceeds */
			line = ini_get_line_number(ini, buf);

			key = malloc(key_len + 1);
			if (!key) goto error;
			key[key_len] = '\0';
			memcpy(key, buf, key_len);

			value = malloc(value_len + 1);
			if (!value) goto key_error;
			value[value_len] = '\0';
			memcpy(value, buf2, value_len);

			printf("[%02i] : Reading key: \'%s\' value: \'%s\'\n", line, key, value);
			dump_if_nonprintable("key", key,key_len);
			dump_if_nonprintable("value", value, value_len);
			free(key);
			key = NULL; /* satisfy gcc's static analyzers */
			free(value);
		}
	}

	ini_close(ini);
	return EXIT_SUCCESS;
key_error:
	free(key);
error:
	ini_close(ini);
	return EXIT_FAILURE;
}
