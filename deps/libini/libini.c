// SPDX-License-Identifier: LGPL-2.1-or-later
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
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ini.h"

struct INI {
	const char *buf, *end, *curr;
	bool free_buf_on_exit;
};

static struct INI * internal_ini_open_mem(const char *buf,
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
	return internal_ini_open_mem(buf, len, false);
}

struct INI *ini_open(const char *file)
{
	FILE *f;
	char *buf, *ptr;
	size_t len, left;
	struct INI *ini = NULL;
	int ret = 0;
	long pos;

	f = fopen(file, "r");
	if (!f) {
		ret = -errno;
		goto err_set_errno;
	}

	/* Determine file size */
	if (fseek(f, 0, SEEK_END)) {
		ret = -errno;
		goto error_fclose;
	}

	pos = ftell(f);
	if (pos < 0) {
		ret = -errno;
		goto error_fclose;
	}

	if (pos == 0) {
		/* empty INI file */
		ret = -EINVAL;
		goto error_fclose;
	}
	if ((unsigned long)pos > SIZE_MAX) {
		/* file too large to fit in size_t */
		ret = -EOVERFLOW;
		goto error_fclose;
	}

	/* rewind, without using rewind, as it can fail without returning errs */
	if (fseek(f, 0, SEEK_SET)) {
		ret = -errno;
		goto error_fclose;
	}

	len = (size_t) pos;
	buf = malloc(len);
	if (!buf) {
		ret = -ENOMEM;
		goto error_fclose;
	}

	for (left = len, ptr = buf; left; ) {
		size_t tmp;
		/* Defensive: do not read on an errored/EOF stream */
		if (ferror(f)) {
			ret = -EIO;
			goto error_free;
		}
		if (feof(f))
			break;

		errno = 0;
		tmp = fread(ptr, 1, left, f);
		if (tmp == 0) {
			if (feof(f))
				break;

			/* fread failed */
			if(ferror(f)) {
				ret = -EIO;
				goto error_free;
			}
			/* Should not happen, but be defensive */
			ret = -EIO;
			goto error_free;
		}

		left -= tmp;
		ptr += tmp;
	}

	ini = internal_ini_open_mem(buf, len - left, true);
	if (!ini) {
		ret = -errno;
		goto error_free;
	}

	/* Intentionally ignore fclose() return value.
	 * Stream opened read-only; no meaningful recovery possible on close failure
	 */
	(void) fclose(f);
	return ini;

error_free:
	free(buf);
error_fclose:
	(void) fclose(f);
err_set_errno:
	errno = -ret;
	return ini;
}

void ini_close(struct INI *ini)
{
	if (!ini)
		return;
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
	if (!ini)
		return -EIO;

	if (ini->curr == ini->end)
		return 0; /* EOF: no more sections */

	/* skip comments at start of file or current position */
	if (ini->curr == ini->buf) {
		if (skip_comments(ini))
			return -EIO;

		// skip leading whitespace before the '[' char
		while (ini->curr < ini->end && (*ini->curr == ' ' || *ini->curr == '\t'))
			ini->curr++;

		if (ini->curr == ini->end || *ini->curr != '[')
			return -EIO;
	} else while (ini->curr < ini->end && *ini->curr != '[' && !skip_line(ini));

	if (ini->curr == ini->end)
		return 0; /* EOF: no more sections */

	/* move past the '[' */
	ini->curr++;

	// skip leading whitespace after the '[' char
	while (ini->curr < ini->end && (*ini->curr == ' ' || *ini->curr == '\t'))
		ini->curr++;

	_name = ini->curr;
	/* scan until closing ']' or end-of-line/end-of-buffer */
	while (ini->curr < ini->end && *ini->curr != ']') {
		if (*ini->curr == '\n' || *ini->curr == '[') {
			/* newline or '[' inside section name is invalid */
			return -EIO;
		}
		ini->curr++;
	}

	/* did we hit the end without finding ']'? */
	if (ini->curr == ini->end)
		return -EIO;

	if (name && name_len) {
		ptrdiff_t tmp_len = ini->curr - _name;
		/* defensive, should never happen */
		if (tmp_len < 0)
			return -EIO;
		/* trim trailing whitespace */
		while (tmp_len > 0 && (_name[tmp_len-1] == ' ' || _name[tmp_len-1] == '\t'))
			tmp_len--;
		/* empty name is bad */
		if (!tmp_len)
			return -EIO;
		*name = _name;
		*name_len = (size_t)tmp_len;
	}

	/* skip over the ']' char */
	ini->curr++;
	/* look at the rest of the line */
	while (ini->curr < ini->end && *ini->curr != '\n') {
		/* let skip_comments() handle it */
		if (*ini->curr == '#')
			break;
		/* eat white space */
		if (*ini->curr == ' ' || *ini->curr == '\t' || *ini->curr == '\r')
			ini->curr++;
		else
			return -EIO;
	}

	return 1;
}

int ini_read_pair(struct INI *ini,
			const char **key, size_t *key_len,
			const char **value, size_t *value_len)
{
	size_t _key_len = 0;
	const char *_key, *_value, *curr, *end;
	ptrdiff_t tmp_len;

	if (!ini)
		return -EIO;
	end = ini->end;

	if (skip_comments(ini))
		return 0;

	/* skip whitespace at the start of the line */
	while (ini->curr < ini->end && (*ini->curr == ' ' || *ini->curr == '\t'))
		ini->curr++;

	curr = _key = ini->curr;

	/* section header, not a key/value pair */
	if (*curr == '[')
		return 0;

	while (true) {
		curr++;

		if (curr == end || *curr == '\n') {
			return -EIO;

		} else if (*curr == '=') {
			const char *tmp;
			tmp_len = curr - _key;

			/* defensive, should never happen */
			if (tmp_len < 0)
				return -EIO;
			_key_len = (size_t)tmp_len;
			for (tmp = curr - 1; tmp > ini->curr &&
					(*tmp == ' ' || *tmp == '\t'); tmp--)
				_key_len--;
			curr++;
			break;
		}
	}

	/* Skip whitespaces after the '=' */
	while (curr != end && (*curr == ' ' || *curr == '\t')) curr++;
	if (curr == end)
		return -EIO;

	_value = curr;

	/* find the end of the value */
	while (curr != end && *curr != '\n') curr++;
	if (curr == end)
		return -EIO;

	*value = _value;
	tmp_len = curr - _value - (*(curr - 1) == '\r');
	/* defensive, should never happen */
	if (tmp_len < 0)
		return -EIO;
	/* trim trailing whitespace */
	while (tmp_len > 0 &&
			(_value[tmp_len - 1] == ' ' || _value[tmp_len - 1] == '\t'))
		tmp_len--;

	*value_len = (size_t)tmp_len;
	*key = _key;
	*key_len = _key_len;

	ini->curr = ++curr;
	return 1;
}

void ini_set_read_pointer(struct INI *ini, const char *pointer)
{
	if (!ini)
		return;

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

	if (!ini)
		return -EIO;
	if ((uintptr_t) pointer < (uintptr_t) ini->buf)
		return -EINVAL;
	if ((uintptr_t) pointer > (uintptr_t) ini->end)
		return -EINVAL;

	for (it = ini->buf; (uintptr_t) it < (uintptr_t) pointer; it++)
		line += (*it == '\n');

	return line;
}
