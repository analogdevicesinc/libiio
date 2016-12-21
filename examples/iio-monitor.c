/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * */

#define _BSD_SOURCE
#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include <cdk/cdk.h>
#include <locale.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#ifdef __APPLE__
#include <iio/iio.h>
#else
#include <iio.h>
#endif

#define ARRAY_SIZE(x) (sizeof(x) ? sizeof(x) / sizeof((x)[0]) : 0)

#define RED	020
#define YELLOW	040
#define BLUE	050

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

static double get_channel_value(struct iio_channel *chn)
{
	char *old_locale;
	char buf[1024];
	double val;

	old_locale = strdup(setlocale(LC_NUMERIC, NULL));
	setlocale(LC_NUMERIC, "C");

	if (channel_has_attr(chn, "input")) {
		iio_channel_attr_read(chn, "input", buf, sizeof(buf));
		val = strtod(buf, NULL);
	} else {
		iio_channel_attr_read(chn, "raw", buf, sizeof(buf));
		val = strtod(buf, NULL);

		if (channel_has_attr(chn, "offset")) {
			iio_channel_attr_read(chn, "offset", buf, sizeof(buf));
			val += strtod(buf, NULL);
		}

		if (channel_has_attr(chn, "scale")) {
			iio_channel_attr_read(chn, "scale", buf, sizeof(buf));
			val *= strtod(buf, NULL);
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
	{ 0, },
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
		(void) row; /* Prevent warning */

		usleep(100000);

		if (selected < 0)
			continue;

		dev = iio_context_get_device(ctx, selected);

		name = iio_device_get_name(dev);
		if (!name)
			name = iio_device_get_id(dev);

		getmaxyx(right, row, col);

		werase(right);

		sprintf(buf, "</B>Device selected: </%u>%s<!%u><!B>",
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

			sprintf(buf, "</%u></B>%s<!B><!%u>",
					BLUE, name, BLUE);
			str = char2Chtype(buf, &len, &align);
			writeChtype(right, 2, line, str,
					HORIZONTAL, 0, len);
			freeChtype(str);

			sprintf(buf, "</%u></B>%.3lf %s<!B><!%u>",
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
	unsigned int num_contexts;
	CDKSCREEN *screen;
	CDKSCROLL *list;
	const char *uri;
	unsigned int i;
	bool free_uri;
	char **items;
	int ret;

	scan_ctx = iio_create_scan_context(NULL, 0);
	if (!scan_ctx)
		return NULL;

	screen = initCDKScreen(win);

	do {
		ret = iio_scan_context_get_info_list(scan_ctx, &info);
		if (ret < 0)
			break;

		num_contexts = ret;

		items = calloc(num_contexts + 1, sizeof(*items));

		for (i = 0; i < num_contexts; i++) {
			 asprintf(&items[i], "</%d>%s<!%d> </%d>[%s]<!%d>", YELLOW,
				iio_context_info_get_description(info[i]),
				YELLOW, BLUE,
				iio_context_info_get_uri(info[i]),
				BLUE);
		}

		items[i] = "Enter location";

		list = newCDKScroll(screen, LEFT, TOP, RIGHT, 0, 0,
				"\n Select a IIO context to use:\n",
				items, num_contexts + 1, TRUE,
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
				popupLabel(screen, msg, 1);
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

	pthread_create(&thd, NULL, read_thd, ctx);

	nb_devices = iio_context_get_devices_count(ctx);
	dev_names = malloc(nb_devices * sizeof(char *));

	for (i = 0; i < nb_devices; i++) {
		char buf[1024];
		struct iio_device *dev = iio_context_get_device(ctx, i);
		const char *name = iio_device_get_name(dev);
		if (!name)
			name = iio_device_get_id(dev);
		sprintf(buf, "</B> %s", name);
		dev_names[i] = strdup(buf);
	}

	boxWindow(right, 0);
	list = newCDKScroll(screen, LEFT, TOP, RIGHT, 0, 0,
			"\n List of available IIO devices:\n",
			dev_names, nb_devices, FALSE,
			A_BOLD | A_REVERSE, TRUE, FALSE);

	drawCDKScroll(list, TRUE);

	while (!stop) {
		int ret = activateCDKScroll(list, NULL);
		stop = ret < 0;
		selected = ret;
		usleep(100000);
	}

	pthread_join(thd, NULL);

	destroyCDKScroll(list);
	for (i = 0; i < nb_devices; i++)
		free(dev_names[i]);
	free(dev_names);
	destroyCDKScreen(screen);
}

int main(void)
{
	struct iio_context *ctx;
	int row, col;

	win = initscr();
	noecho();
	keypad(win, TRUE);
	getmaxyx(win, row, col);
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
