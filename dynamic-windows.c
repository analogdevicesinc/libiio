// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "dynamic.h"
#include "iio-backend.h"
#include "iio-private.h"

#include <errno.h>
#include <windows.h>

struct iio_directory {
	char *lastpath;
	HANDLE file;
};

void * iio_dlopen(const char *path)
{
	return LoadLibrary(TEXT(path));
}

void iio_dlclose(void *lib)
{
	FreeLibrary((void *) module);
}

const void * iio_dlsym(void *lib, const char *symbol)
{
	return (const void *) GetProcAddress(lib, symbol);
}

struct iio_directory * iio_open_dir(const char *path)
{
	struct iio_directory *dir;
	WIN32_FIND_DATA fdata;
	char buf[PATH_MAX];

	dir = zalloc(sizeof(*dir));
	if (!dir)
		return ERR_PTR(-ENOMEM);

	iio_snprintf(buf, sizeof(buf), "%s\\*", path);

	/* FindFirstFileA will always return the '.' folder if the directory
	 * exists, so we don't care if the related fdata is lost. */
	dir->file = FindFirstFileA(buf, &fdata);
	if (dir->file == INVALID_HANDLE_VALUE) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
			return ERR_PTR(-ENOENT);
		else
			return ERR_PTR(-EIO);
	}

	return dir;
}

void iio_close_dir(struct iio_directory *dir)
{
	if (dir->file)
		FindClose(dir->file);
	free (dir->lastpath);
	free(dir);
}

const char * iio_dir_get_next_file_name(struct iio_directory *dir)
{
	WIN32_FIND_DATA fdata;

	free(dir->lastpath);

	do {
		if (!FindNextFileA(dir->file, &fdata))
			return NULL;
	} while (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);

	dir->lastpath = _strdup(fdata.cFileName);

	return dir->lastpath;
}
