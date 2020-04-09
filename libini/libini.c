/*
 * libini - Library to read INI configuration files
 *
 * Copyright (C) 2014 Paul Cercueil <paul@crapouillou.net>
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
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ini.h"

struct INI {
	const char *buf, *end, *curr;
	bool free_buf_on_exit;
};

static struct INI *_ini_open_mem(const char *buf,
			size_t len, bool free_buf_on_exit)
{
	struct INI *ini = malloc(sizeof(*ini));
	if (!ini) {
		errno = ENOMEM;
		return NULL;
	}

	ini->buf = ini->curr = buf;
	ini->end = buf + len;
	ini->free_buf_on_exit = free_buf_on_exit;
	return ini;
}

struct INI *ini_open_mem(const char *buf, size_t len)
{
	return _ini_open_mem(buf, len, false);
}

struct INI *ini_open(const char *file)
{
	FILE *f;
	char *buf, *ptr;
	size_t len, left;
	struct INI *ini = NULL;
	int ret = 0;

	f = fopen(file, "r");
	if (!f) {
		ret = -errno;
		goto err_set_errno;
	}

	fseek(f, 0, SEEK_END);
	ret = ftell(f);

	if (ret <= 0) {
		ret = -EINVAL;
		goto error_fclose;
	}

	len = (size_t) ret;
	buf = malloc(len);
	if (!buf) {
		ret = -ENOMEM;
		goto error_fclose;
	}

	rewind(f);

	for (left = len, ptr = buf; left; ) {
		size_t tmp = fread(ptr, 1, left, f);
		if (tmp == 0) {
			if (feof(f))
				break;

			ret = -ferror(f);
			free(buf);
			goto error_fclose;
		}

		left -= tmp;
		ptr += tmp;
	}

	ini = _ini_open_mem(buf, len - left, true);
	if (!ini)
		ret = -errno;

error_fclose:
	fclose(f);
err_set_errno:
	errno = -ret;
	return ini;
}

void ini_close(struct INI *ini)
{
	if (ini->free_buf_on_exit)
		free((char *) ini->buf);
	free(ini);
}

static bool skip_comments(struct INI *ini)
{
	const char *curr = ini->curr;
	const char *end = ini->end;

	while (curr != end) {
		if (*curr == '\r' || *curr == '\n')
			curr++;
		else if (*curr == '#')
			do { curr++; } while (curr != end && *curr != '\n');
		else
			break;
	}

	ini->curr = curr;
	return curr == end;
}

static bool skip_line(struct INI *ini)
{
	const char *curr = ini->curr;
	const char *end = ini->end;

	for (; curr != end && *curr != '\n'; curr++);
	if (curr == end) {
		ini->curr = end;
		return true;
	} else {
		ini->curr = curr + 1;
		return false;
	}
}

int ini_next_section(struct INI *ini, const char **name, size_t *name_len)
{
	const char *_name;
	if (ini->curr == ini->end)
		return 0; /* EOF: no more sections */

	if (ini->curr == ini->buf) {
		if (skip_comments(ini) || *ini->curr != '[')
			return -EIO;
	} else while (*ini->curr != '[' && !skip_line(ini));

	if (ini->curr == ini->end)
		return 0; /* EOF: no more sections */

	_name = ++ini->curr;
	do {
		ini->curr++;
		if (ini->curr == ini->end || *ini->curr == '\n')
			return -EIO;
	} while (*ini->curr != ']');


	if (name && name_len) {
		*name = _name;
		*name_len = ini->curr - _name;
	}

	ini->curr++;
	return 1;
}

int ini_read_pair(struct INI *ini,
			const char **key, size_t *key_len,
			const char **value, size_t *value_len)
{
	size_t _key_len = 0;
	const char *_key, *_value, *curr, *end = ini->end;

	if (skip_comments(ini))
		return 0;
	curr = _key = ini->curr;

	if (*curr == '[')
		return 0;

	while (true) {
		curr++;

		if (curr == end || *curr == '\n') {
			return -EIO;

		} else if (*curr == '=') {
			const char *tmp = curr;
			_key_len = curr - _key;
			for (tmp = curr - 1; tmp > ini->curr &&
					(*tmp == ' ' || *tmp == '\t'); tmp--)
				_key_len--;
			curr++;
			break;
		}
	}

	/* Skip whitespaces. */
	while (curr != end && (*curr == ' ' || *curr == '\t')) curr++;
	if (curr == end)
		return -EIO;

	_value = curr;

	while (curr != end && *curr != '\n') curr++;
	if (curr == end)
		return -EIO;

	*value = _value;
	*value_len = curr - _value - (*(curr - 1) == '\r');
	*key = _key;
	*key_len = _key_len;

	ini->curr = ++curr;
	return 1;
}

void ini_set_read_pointer(struct INI *ini, const char *pointer)
{
	if ((uintptr_t) pointer < (uintptr_t) ini->buf)
		ini->curr = ini->buf;
	else if ((uintptr_t) pointer > (uintptr_t) ini->end)
		ini->curr = ini->end;
	else
		ini->curr = pointer;
}

int ini_get_line_number(struct INI *ini, const char *pointer)
{
	int line = 1;
	const char *it;

	if ((uintptr_t) pointer < (uintptr_t) ini->buf)
		return -EINVAL;
	if ((uintptr_t) pointer > (uintptr_t) ini->end)
		return -EINVAL;

	for (it = ini->buf; (uintptr_t) it < (uintptr_t) pointer; it++)
		line += (*it == '\n');

	return line;
}
