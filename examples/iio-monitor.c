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

#include <cdk/cdk.h>
#include <iio.h>
#include <pthread.h>
#include <stdbool.h>

#define ARRAY_SIZE(x) (sizeof(x) ? sizeof(x) / sizeof((x)[0]) : 0)

#define RED	020
#define YELLOW	040
#define BLUE	050

static int selected = -1;
static pthread_t thd;

static WINDOW *win, *left, *right;
struct iio_context *ctx;
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
		 channel_has_attr(chn, "processed"));
}

static double get_channel_value(struct iio_channel *chn)
{
	char buf[1024];
	double val;

	if (channel_has_attr(chn, "processed")) {
		iio_channel_attr_read(chn, "processed", buf, sizeof(buf));
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

	val = (double) ((unsigned long) val) / 1000.0;
	return val;
}

static void * read_thd(void *d)
{
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

		eraseCursesWindow(right);
		boxWindow(right, 0);

		sprintf(buf, "</B>Device selected: </%u>%s<!%u><!B>",
				RED, name, RED);
		str = char2Chtype(buf, &len, &align);
		writeChtype(right, 2, line, str, HORIZONTAL, 0, len);
		line += 2;

		nb_channels = iio_device_get_channels_count(dev);
		for (i = 0; i < nb_channels; i++) {
			const char *id;
			bool is_temp = false;
			struct iio_channel *chn =
				iio_device_get_channel(dev, i);
			if (!is_valid_channel(chn))
				continue;

			nb++;
			name = iio_channel_get_name(chn);
			id = iio_channel_get_id(chn);
			if (!name)
				name = id;
			is_temp = !strncmp(id, "temp", 4);

			sprintf(buf, "</%u></B>%s<!B><!%u>",
					BLUE, name, BLUE);
			str = char2Chtype(buf, &len, &align);
			writeChtype(right, 2, line, str,
					HORIZONTAL, 0, len);

			sprintf(buf, "</%u></B>%.3lf %s<!B><!%u>",
					YELLOW, get_channel_value(chn),
					is_temp ? "°C" : "V", YELLOW);
			str = char2Chtype(buf, &len, &align);
			writeChtype(right, col / 2, line++,
					str, HORIZONTAL, 0, len);
		}

		if (nb == 0) {
			char msg[] = "No valid input channels found.";
			writeChar(right, 2, line++, msg,
					HORIZONTAL, 0, sizeof(msg) - 1);
		}

		wrefresh(right);
	}
	return NULL;
}

int main()
{
	CDKSCREEN *screen;
	CDKSCROLL *list;
	int row, col;
	unsigned int i, nb_devices;

	char **dev_names;

	ctx = iio_create_local_context();

	win = initscr();
	noecho();
	keypad(win, TRUE);
	getmaxyx(win, row, col);
	initCDKColor();
	screen = initCDKScreen(win);

	left = newwin(row, col / 2, 0, 0);
	right = newwin(row, col / 2, 0, col / 2);

	if (ctx) {
		char *title[] = {
			"Do you want to connect to a remote IIOD server?",
		};
		char *buttons[] = {
			"No",
			"Yes",
		};
		int ret = popupDialog(screen, title, ARRAY_SIZE(title),
				buttons, ARRAY_SIZE(buttons));
		if (ret == 1) {
			iio_context_destroy(ctx);
			ctx = NULL;
		}
	}

	if (!ctx) {
		char *hostname = getString(screen,
				"Please enter the IP or hostname of the server",
				"Hostname:  ", "localhost");
		ctx = iio_create_network_context(hostname);
		if (!ctx)
			goto err_destroy_cdk;
	}

	destroyCDKScreen(screen);
	screen = initCDKScreen(left);

	pthread_create(&thd, NULL, read_thd, NULL);

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

	iio_context_destroy(ctx);

	destroyCDKScroll(list);
	for (i = 0; i < nb_devices; i++)
		free(dev_names[i]);
	free(dev_names);
err_destroy_cdk:
	destroyCDKScreen(screen);
	endCDK();
	delwin(left);
	delwin(right);
	return 0;
}
