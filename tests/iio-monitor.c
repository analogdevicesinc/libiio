// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 * */

#ifndef _BSD_SOURCE
#define _BSD_SOURCE 1
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#include <cdk.h>
#include <locale.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <iio.h>

#ifdef _MSC_BUILD
#define inline __inline
#define iio_snprintf sprintf_s
#else
#define iio_snprintf snprintf
#endif

#define ARRAY_SIZE(x) (sizeof(x) ? sizeof(x) / sizeof((x)[0]) : 0)

#define RED	020u
#define YELLOW	040u
#define BLUE	050u

static int selected = -1;

static WINDOW *win, *left, *right;
static bool stop;

static bool channel_has_attr(struct iio_channel *chn, const char *attr)
{
	unsigned int i, nb = iio_channel_get_attrs_count(chn);
	for (i = 0; i < nb; i++)
		if (!strcmp(attr, iio_channel_get_attr(chn, i)))
			return true;
	return false;
}

static bool is_valid_channel(struct iio_channel *chn)
{
	return !iio_channel_is_output(chn) &&
		(channel_has_attr(chn, "raw") ||
		 channel_has_attr(chn, "input"));
}

static void err_str(int ret)
{
	char buf[256];
	iio_strerror(-ret, buf, sizeof(buf));
	fprintf(stderr, "Error during read: %s\n", buf);
}

static double get_channel_value(struct iio_channel *chn)
{
	char *old_locale, *end;
	char buf[1024];
	double val;
	int ret;

	old_locale = strdup(setlocale(LC_NUMERIC, NULL));
	setlocale(LC_NUMERIC, "C");

	if (channel_has_attr(chn, "input")) {
		ret = iio_channel_attr_read(chn, "input", buf, sizeof(buf));
		if (ret < 0) {
			err_str(ret);
			val = 0;
		} else {
			errno = 0;
			val = strtod(buf, &end);
			if (buf == end || errno == ERANGE) {
				fprintf(stderr, "issue decoding '%s' to decimal\n", buf);
				val = 0;
			}
		}
	} else {
		ret = iio_channel_attr_read(chn, "raw", buf, sizeof(buf));
		if (ret < 0) {
			err_str(ret);
			val = 0;
		} else {
			errno = 0;
			val = strtod(buf, &end);
			if (buf == end || errno == ERANGE) {
				fprintf(stderr, "issue decoding '%s' to decimal\n", buf);
				val = 0;
			}
		}
		if (channel_has_attr(chn, "offset")) {
			ret = iio_channel_attr_read(chn, "offset", buf, sizeof(buf));
			if (ret < 0)
				err_str(ret);
			else {
				errno = 0;
				val += strtod(buf, &end);
				if (buf == end || errno == ERANGE)
					fprintf(stderr, "issue decoding '%s' to decimal\n", buf);
			}
		}

		if (channel_has_attr(chn, "scale")) {
			ret = iio_channel_attr_read(chn, "scale", buf, sizeof(buf));
			if (ret < 0)
				err_str(ret);
			else {
				errno = 0;
				val *= strtod(buf, &end);
				if (buf == end || errno == ERANGE)
					fprintf(stderr, "issue decoding '%s' to decimal\n", buf);
			}
		}
	}

	setlocale(LC_NUMERIC, old_locale);
	free(old_locale);

	return val / 1000.0;
}

static struct {
	const char *id;
	const char *unit;
} map[] = {
	{ "current",	"A" },
	{ "power",	"W" },
	{ "temp",	"°C" },
	{ "voltage",	"V" },
	{ NULL, NULL },
};

static const char *id_to_unit(const char *id)
{
	unsigned int i;

	for (i = 0; map[i].id; i++) {
		if (!strncmp(id, map[i].id, strlen(map[i].id)))
			return map[i].unit;
	}

	return "";
}

static void * read_thd(void *d)
{
	struct iio_context *ctx = d;

	while (!stop) {
		struct iio_device *dev;
		const char *name;
		int row, col, len, align, line = 2;
		unsigned int i, nb_channels, nb = 0;
		char buf[1024];
		chtype *str;
		struct timespec wait;
		(void) row; /* Prevent warning */

		wait.tv_sec = 0;
		wait.tv_nsec = (100000 * 1000);
		nanosleep(&wait, &wait);

		if (selected < 0)
			continue;

		dev = iio_context_get_device(ctx, selected);

		name = iio_device_get_name(dev);
		if (!name)
			name = iio_device_get_id(dev);

		getmaxyx(right, row, col);

		werase(right);

		iio_snprintf(buf, sizeof(buf), "</B>Device selected: </%u>%s<!%u><!B>",
				RED, name, RED);
		str = char2Chtype(buf, &len, &align);
		writeChtype(right, 2, line, str, HORIZONTAL, 0, len);
		freeChtype(str);
		line += 2;

		nb_channels = iio_device_get_channels_count(dev);
		for (i = 0; i < nb_channels; i++) {
			const char *id;
			const char *unit;
			struct iio_channel *chn =
				iio_device_get_channel(dev, i);
			if (!is_valid_channel(chn))
				continue;

			nb++;
			name = iio_channel_get_name(chn);
			id = iio_channel_get_id(chn);
			if (!name)
				name = id;
			unit = id_to_unit(id);

			iio_snprintf(buf, sizeof(buf), "</%u></B>%s<!B><!%u>",
					BLUE, name, BLUE);
			str = char2Chtype(buf, &len, &align);
			writeChtype(right, 2, line, str,
					HORIZONTAL, 0, len);
			freeChtype(str);

			iio_snprintf(buf, sizeof(buf), "</%u></B>%.3lf %s<!B><!%u>",
					YELLOW, get_channel_value(chn), unit,
					YELLOW);
			str = char2Chtype(buf, &len, &align);
			writeChtype(right, col / 2, line++,
					str, HORIZONTAL, 0, len);
			freeChtype(str);
		}

		if (nb == 0) {
			char msg[] = "No valid input channels found.";
			writeChar(right, 2, line++, msg,
					HORIZONTAL, 0, sizeof(msg) - 1);
		}

		boxWindow(right, 0);
	}
	return NULL;
}

static struct iio_context *show_contexts_screen(void)
{
	struct iio_context *ctx = NULL;
	struct iio_scan_context *scan_ctx;
	struct iio_context_info **info;
	int num_contexts;
	CDKSCREEN *screen;
	CDKSCROLL *list;
	const char *uri;
	int i;
	bool free_uri;
	char **items;
	int ret;

	scan_ctx = iio_create_scan_context(NULL, 0);
	if (!scan_ctx)
		return NULL;

	screen = initCDKScreen(win);
	if (!screen) {
		fprintf(stderr, "out of memory\n");
		goto scan_err;
	}

	do {
		ret = iio_scan_context_get_info_list(scan_ctx, &info);
		if (ret < 0)
			break;

		num_contexts = ret;

		items = calloc(num_contexts + 1, sizeof(*items));
		if (!items) {
			fprintf(stderr, "calloc failed, out of memory\n");
			break;
		}

		for (i = 0; i < num_contexts; i++) {
			ret = asprintf(&items[i], "</%d>%s<!%d> </%d>[%s]<!%d>", YELLOW,
				iio_context_info_get_description(info[i]),
				YELLOW, BLUE,
				iio_context_info_get_uri(info[i]),
				BLUE);
			if (ret < 0) {
				fprintf(stderr, "asprintf failed, out of memory?\n");
				break;
			}
		}
		if (ret < 0) {
			break;
		}

		items[i] = "Enter location";

		list = newCDKScroll(screen, LEFT, TOP, RIGHT, 0, 0,
				"\n Select a IIO context to use:\n",
				(CDK_CSTRING2) items, num_contexts + 1, TRUE,
				A_BOLD | A_REVERSE, TRUE, FALSE);

		drawCDKScroll(list, TRUE);

		ret = activateCDKScroll(list, NULL);
		if (ret < num_contexts) {
			uri = iio_context_info_get_uri(info[ret]);
			free_uri = FALSE;
		} else if (ret == num_contexts) {
			uri = getString(screen,
					"Please enter the location of the server",
					"Location:  ", "ip:localhost");
			free_uri = TRUE;
		} else {
			uri = NULL;
		}

		if (uri) {
			ctx = iio_create_context_from_uri(uri);
			if (ctx == NULL) {
				char *msg[] = { "</16>Failed to create IIO context.<!16>" };
				popupLabel(screen, (CDK_CSTRING2)msg, 1);
			}

			if (free_uri)
				freeChar((char *)uri);
		}

		destroyCDKScroll(list);
		iio_context_info_list_free(info);
		for (i = 0; i < num_contexts; i++)
			free(items[i]);
		free(items);

	} while (!ctx && ret >= 0);

	destroyCDKScreen(screen);

scan_err:
	iio_scan_context_destroy(scan_ctx);

	return ctx;
}

static void show_main_screen(struct iio_context *ctx)
{
	unsigned int i, nb_devices;
	CDKSCREEN *screen;
	char **dev_names;
	CDKSCROLL *list;
	pthread_t thd;

	stop = FALSE;
	screen = initCDKScreen(left);
	if (!screen) {
		stop = TRUE;
		fprintf(stderr, "initCDKScreen failed, out of memory\n");
		return;
	}

	if (pthread_create(&thd, NULL, read_thd, ctx)) {
		fprintf(stderr, "problem with pthread_create\n");
		goto thread_err;
	}

	nb_devices = iio_context_get_devices_count(ctx);
	dev_names = calloc(nb_devices, sizeof(char *));
	if (!dev_names) {
		fprintf(stderr, "calloc failed, out of memory\n");
		goto name_err;
	}

	for (i = 0; i < nb_devices; i++) {
		char buf[1024];
		struct iio_device *dev = iio_context_get_device(ctx, i);
		const char *name = iio_device_get_name(dev);
		if (!name)
			name = iio_device_get_id(dev);
		iio_snprintf(buf, sizeof(buf), "</B> %s", name);
		dev_names[i] = strdup(buf);
		if (!dev_names[i])
			goto dev_name_err;
	}

	boxWindow(right, 0);
	list = newCDKScroll(screen, LEFT, TOP, RIGHT, 0, 0,
			"\n List of available IIO devices:\n",
			(CDK_CSTRING2) dev_names, nb_devices, FALSE,
			A_BOLD | A_REVERSE, TRUE, FALSE);

	drawCDKScroll(list, TRUE);

	while (!stop) {
		int ret = activateCDKScroll(list, NULL);
		struct timespec wait;
		wait.tv_sec = 0;
		wait.tv_nsec = (100000 * 1000);

		stop = ret < 0;
		selected = ret;
		nanosleep(&wait, &wait);
	}

	pthread_join(thd, NULL);

	destroyCDKScroll(list);
	for (i = 0; i < nb_devices; i++)
		free(dev_names[i]);
	free(dev_names);
	destroyCDKScreen(screen);

	return;

dev_name_err:
	for (i = 0; i < nb_devices; i++) {
		if (dev_names[i])
			free(dev_names[i]);
	}
	free(dev_names);
name_err:
	stop = TRUE;
	pthread_join(thd, NULL);
thread_err:
	destroyCDKScreen(screen);

	return;
}

int main(void)
{
	struct iio_context *ctx;
	int row, col;

	win = initscr();
	noecho();
	keypad(win, TRUE);
	getmaxyx(win, row, col);

	/* If this was started from a small window, quit */
	if (row < 10 || col < 50) {
		endwin();
		fprintf(stderr, "Sorry, I need a bigger window,\n"
				"min is 10 x 50\n");
		return 0;
	}

	initCDKColor();

	left = newwin(row, col / 2, 0, 0);
	right = newwin(row, col / 2, 0, col / 2);

	while (TRUE) {
		ctx = show_contexts_screen();
		if (!ctx)
			break;

		show_main_screen(ctx);
		iio_context_destroy(ctx);
	}

	endCDK();
	delwin(left);
	delwin(right);
	return 0;
}
