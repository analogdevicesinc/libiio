// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>

struct iio_directory {
	DIR *directory;
};

struct iio_module * iio_open_module(const char *path)
{
	return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
}

void iio_release_module(struct iio_module *module)
{
	dlclose((void *) module);
}

const struct iio_backend *
iio_module_get_backend(struct iio_module *module, const char *symbol)
{
	return dlsym(module, symbol);
}

struct iio_directory * iio_open_dir(const char *path)
{
	struct iio_directory *entry;

	entry = malloc(sizeof(*entry));
	if (!entry)
		return ERR_PTR(-ENOMEM);

	entry->directory = opendir(path);
	if (!entry->directory) {
		free(entry);
		return ERR_PTR(-errno);
	}

	return entry;
}

void iio_close_dir(struct iio_directory *dir)
{
	closedir(dir->directory);
	free(dir);
}

const char * iio_dir_get_next_file_name(struct iio_directory *dir)
{
	struct dirent *dirent;

	dirent = readdir(dir->directory);
	if (!dirent)
		return NULL;

	return dirent->d_name;
}
