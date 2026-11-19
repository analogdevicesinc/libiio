// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * ASCII (libiio v0.x) protocol command handlers for the Zephyr iiod server.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * NOTE: LGPL-2.1-or-later because this file incorporates code from the
 * LGPL-2.1 iiod sources: read_line() and the grammar-parser ascii_interpreter()
 * derive from iiod/ops.c, and do_version()/do_print()/do_zprint() are taken
 * from the iiod/parser.y grammar actions. To be relicensed to MIT together
 * with ops.c/ops.h/lexer.l/parser.y once that relicensing lands.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../iiod/ops.h"
#include "iio/iio.h"

#if WITH_IIOD_V0_COMPAT_GRAMMAR_PARSER
#include "parser.h"

int yyparse(yyscan_t scanner);
#endif

/* Stub - to be implemented. */
int set_buffers_count(struct parser_pdata *pdata, struct iio_device *dev, long value)
{
	(void)dev;
	(void)value;

	print_value(pdata, -ENOSYS);
	return -ENOSYS;
}

/* Stub - to be implemented. */
int open_dev(struct parser_pdata *pdata, struct iio_device *dev, size_t samples_count,
		const char *mask, bool cyclic)
{
	(void)dev;
	(void)samples_count;
	(void)mask;
	(void)cyclic;

	print_value(pdata, -ENOSYS);
	return -ENOSYS;
}

/* Stub - to be implemented. */
int close_dev(struct parser_pdata *pdata, struct iio_device *dev)
{
	(void)pdata;
	(void)dev;

	/* No device can be open yet (open_dev fails), so this is only reached
	 * via the explicit CLOSE command. Reply quietly with success to keep the
	 * client's state machine happy. TODO: check flow reaching this point*/
	return 0;
}

/* Stub - to be implemented. */
ssize_t rw_dev(struct parser_pdata *pdata, struct iio_device *dev, unsigned int nb, bool is_write)
{
	(void)dev;
	(void)nb;
	(void)is_write;

	print_value(pdata, -ENOSYS);
	return -ENOSYS;
}

/*
 * Differs from invalidate_sample_size_cache() in ops.c: reimplemented as a
 * no-op. Called from responder.c (binary path) when a channel's format changes.
 * The ops.c version signals the per-device RW thread to recompute the sample
 * size; there is no such thread here, so nothing needs to be done. Still
 * provided because responder.c references it whenever WITH_IIOD_V0_COMPAT is
 * set, so the symbol must exist at link time.
 */
void invalidate_sample_size_cache(const struct iio_device *dev)
{
	(void)dev;
}

/* Differs from read_line() in ops.c: reimplemented with only the byte-at-a-time
 * path. ops.c also has a socket branch (recv() with MSG_PEEK/MSG_TRUNC) and a
 * USB bulk branch, both of which can read past the newline; that would corrupt
 * the ASCII->binary handoff on the Zephyr transports. Here we read one byte at
 * a time until the newline, so nothing past the line is ever consumed. */
ssize_t read_line(struct parser_pdata *pdata, char *buf, size_t len)
{
	size_t bytes_read = 0;
	bool found;

	while (len) {
		ssize_t ret = pdata->readfd(pdata, buf, 1);
		if (ret <= 0)
			return ret;

		bytes_read++;

		if (*buf == '\n')
			break;

		len--;
		buf++;
	}

	found = !!len;

	return found ? (ssize_t)bytes_read : -EIO;
}

/*
 * The recognize-and-dispatch layer is build-time selectable (see the
 * "v0 ASCII command parser implementation" Kconfig choice). Both variants
 * recognize the same language and drive the shared command handlers in
 * iiod/ops-common.c identically; only the way a command line is parsed differs.
 */
#if WITH_IIOD_V0_COMPAT_GRAMMAR_PARSER

/*
 * flex/bison front end: recognize commands with the generated scanner/parser
 * (iiod/lexer.l, iiod/parser.y).
 *
 * Differs from ascii_interpreter() in ops.c: the yylex/yyparse loop is the
 * same, but the trailing per-device cleanup loop (close_dev_helper() over every
 * device) is omitted. That loop tears down the POSIX RW threads, which do not
 * exist here; no device can be open yet, so there is nothing to tear down.
 */
void ascii_interpreter(struct parser_pdata *pdata)
{
	yyscan_t scanner;
	int ret;

	yylex_init_extra(pdata, &scanner);

	do {
		ret = yyparse(scanner);
	} while (!pdata->stop && !pdata->binary && ret >= 0);

	yylex_destroy(scanner);
}

#else /* !WITH_IIOD_V0_COMPAT_GRAMMAR_PARSER */

/*
 * Hand-written replacement for the flex/bison front end (iiod/lexer.l and
 * iiod/parser.y). The v0 ASCII grammar has a single non-recursive production
 * (a flat list of "KEYWORD [arg]... <newline>" commands), so a line reader plus
 * positional dispatch recognizes exactly the same language as the generated
 * scanner/parser - without the DFA/LALR tables or the flex+bison build
 * dependency. The command handlers are unchanged; only the
 * recognize-and-dispatch layer differs.
 */

/* Longest command line we accept. v0 commands are short; the only long operand
 * is the OPEN channel bitmask (one hex nibble per 4 channels). */
#define ASCII_CMD_LINE_MAX 512
/* OPEN <dev> <samples> <mask> CYCLIC and WRITE <dev> INPUT <chn> <attr> <len>
 * are the widest commands, at 5 and 6 tokens; 8 leaves headroom to detect
 * trailing garbage as an error rather than silently ignoring it. */
#define ASCII_MAX_TOKENS 8

static void reply_einval(struct parser_pdata *pdata)
{
	/* Matches yyerror(): emit -EINVAL and resync to the next line. */
	print_value(pdata, -EINVAL);
}

/* Keyword match honoring the lexer's exact-case rule: keywords were accepted
 * only as all-uppercase or all-lowercase (e.g. "VERSION|version"), never mixed. */
static bool kw(const char *tok, const char *upper, const char *lower)
{
	return !strcmp(tok, upper) || !strcmp(tok, lower);
}

/* INPUT|input|OUTPUT|output, as the lexer's IN_OUT rule. */
static bool is_in_out(const char *tok)
{
	return kw(tok, "INPUT", "input") || kw(tok, "OUTPUT", "output");
}

/* Resolve and stash the device, mirroring the lexer's WANT_DEVICE action.
 * Returns NULL if not found; the handlers map NULL to -ENODEV. */
static struct iio_device *resolve_dev(struct parser_pdata *pdata, const char *name)
{
	struct iio_device *dev = iio_context_find_device(pdata->ctx, name);
	pdata->dev = dev;
	return dev;
}

/* Resolve and stash the channel, mirroring the lexer's IN_OUT + WANT_CHN
 * actions (channel_is_output drives the input/output lookup). */
static struct iio_channel *resolve_chn(
		struct parser_pdata *pdata, const char *dir, const char *name)
{
	struct iio_channel *chn = NULL;

	pdata->channel_is_output = dir[0] == 'o' || dir[0] == 'O';
	if (pdata->dev)
		chn = iio_device_find_channel(pdata->dev, name, pdata->channel_is_output);
	pdata->chn = chn;
	return chn;
}

/* Split the line in place on runs of spaces/tabs, collapsing them like the
 * lexer's "[ \t]+" rule. Leading whitespace is skipped (lenient: the generated
 * parser rejected it as -EINVAL, but no v0 client emits it). Returns the token
 * count, or -1 if the line holds more than max tokens. */
static int tokenize(char *s, char *argv[], int max)
{
	int argc = 0;

	while (*s) {
		while (*s == ' ' || *s == '\t')
			s++;
		if (!*s)
			break;
		if (argc == max)
			return -1;
		argv[argc++] = s;
		while (*s && *s != ' ' && *s != '\t')
			s++;
		if (*s)
			*s++ = '\0';
	}

	return argc;
}

static void do_version(struct parser_pdata *pdata)
{
	char buf[128];

	snprintf(buf, sizeof(buf), "%u.%u.%-7.7s\n", LIBIIO_VERSION_MAJOR, LIBIIO_VERSION_MINOR,
			LIBIIO_VERSION_GIT);
	output(pdata, buf);
}

static void do_print(struct parser_pdata *pdata)
{
	char *xml = iio_context_get_xml(pdata->ctx);
	char buf[128];
	int err = iio_err(xml);

	if (err) {
		snprintf(buf, sizeof(buf), "%d\n", err);
		output(pdata, buf);
		return;
	}

	snprintf(buf, sizeof(buf), "%lu\n", (unsigned long)strlen(xml));
	output(pdata, buf);
	output(pdata, xml);
	output(pdata, "\n");
	free(xml);
}

static void do_zprint(struct parser_pdata *pdata)
{
	char buf[128];

	if (pdata->xml_zstd) {
		snprintf(buf, sizeof(buf), "%lu\n", (unsigned long)pdata->xml_zstd_len);
		output(pdata, buf);
		if (write_all(pdata, pdata->xml_zstd, pdata->xml_zstd_len) <= 0)
			pdata->stop = true;
		output(pdata, "\n");
	} else {
		snprintf(buf, sizeof(buf), "%d\n", -EINVAL);
		output(pdata, buf);
	}
}

/* READ <device> [ DEBUG|BUFFER [<attr>] | INPUT|OUTPUT <chn> [<attr>] | <attr> ] */
static void handle_read(struct parser_pdata *pdata, char *argv[], int argc)
{
	struct iio_device *dev = resolve_dev(pdata, argv[1]);

	if (argc == 2) {
		read_dev_attr(pdata, dev, NULL, IIO_ATTR_TYPE_DEVICE);
	} else if (kw(argv[2], "DEBUG", "debug")) {
		if (argc > 4)
			reply_einval(pdata);
		else
			read_dev_attr(pdata, dev, argc == 4 ? argv[3] : NULL, IIO_ATTR_TYPE_DEBUG);
	} else if (kw(argv[2], "BUFFER", "buffer")) {
		if (argc > 4)
			reply_einval(pdata);
		else
			read_dev_attr(pdata, dev, argc == 4 ? argv[3] : NULL, IIO_ATTR_TYPE_BUFFER);
	} else if (is_in_out(argv[2])) {
		if (argc < 4 || argc > 5) {
			reply_einval(pdata);
		} else {
			struct iio_channel *chn = resolve_chn(pdata, argv[2], argv[3]);
			read_chn_attr(pdata, chn, argc == 5 ? argv[4] : NULL);
		}
	} else if (argc == 3) {
		read_dev_attr(pdata, dev, argv[2], IIO_ATTR_TYPE_DEVICE);
	} else {
		reply_einval(pdata);
	}
}

/* WRITE <device> [ DEBUG|BUFFER [<attr>] | INPUT|OUTPUT <chn> [<attr>] | <attr> ] <len>.
 * The last token is always the byte count; the handlers read that many payload
 * bytes themselves via read_all(). */
static void handle_write(struct parser_pdata *pdata, char *argv[], int argc)
{
	struct iio_device *dev = resolve_dev(pdata, argv[1]);

	if (kw(argv[2], "DEBUG", "debug")) {
		if (argc == 4)
			write_dev_attr(pdata, dev, NULL, atol(argv[3]), IIO_ATTR_TYPE_DEBUG);
		else if (argc == 5)
			write_dev_attr(pdata, dev, argv[3], atol(argv[4]), IIO_ATTR_TYPE_DEBUG);
		else
			reply_einval(pdata);
	} else if (kw(argv[2], "BUFFER", "buffer")) {
		if (argc == 4)
			write_dev_attr(pdata, dev, NULL, atol(argv[3]), IIO_ATTR_TYPE_BUFFER);
		else if (argc == 5)
			write_dev_attr(pdata, dev, argv[3], atol(argv[4]), IIO_ATTR_TYPE_BUFFER);
		else
			reply_einval(pdata);
	} else if (is_in_out(argv[2])) {
		struct iio_channel *chn;

		if (argc != 5 && argc != 6) {
			reply_einval(pdata);
			return;
		}
		chn = resolve_chn(pdata, argv[2], argv[3]);
		if (argc == 5)
			write_chn_attr(pdata, chn, NULL, atol(argv[4]));
		else
			write_chn_attr(pdata, chn, argv[4], atol(argv[5]));
	} else if (argc == 3) {
		write_dev_attr(pdata, dev, NULL, atol(argv[2]), IIO_ATTR_TYPE_DEVICE);
	} else if (argc == 4) {
		write_dev_attr(pdata, dev, argv[2], atol(argv[3]), IIO_ATTR_TYPE_DEVICE);
	} else {
		reply_einval(pdata);
	}
}

/* Recognize and execute one ASCII command line. Equivalent to one iteration of
 * the generated parser's Line production (including its "error END" recovery,
 * which replied -EINVAL and moved on). */
static void dispatch_line(struct parser_pdata *pdata, char *line)
{
	char *argv[ASCII_MAX_TOKENS];
	const char *cmd;
	int argc = tokenize(line, argv, ASCII_MAX_TOKENS);

	if (argc == 0)
		return; /* empty line: accepted with no output */
	if (argc < 0) {
		reply_einval(pdata);
		return;
	}

	cmd = argv[0];

	if (kw(cmd, "BINARY", "binary") && argc == 1) {
		enable_binary(pdata);
	} else if ((kw(cmd, "EXIT", "exit") || kw(cmd, "QUIT", "quit")) && argc == 1) {
		/* Ignore EXIT/QUIT, as the generated parser did. */
	} else if (kw(cmd, "VERSION", "version") && argc == 1) {
		do_version(pdata);
	} else if (kw(cmd, "PRINT", "print") && argc == 1) {
		do_print(pdata);
	} else if (kw(cmd, "ZPRINT", "zprint") && argc == 1) {
		do_zprint(pdata);
	} else if (kw(cmd, "TIMEOUT", "timeout") && argc == 2) {
		set_timeout(pdata, atoi(argv[1]));
	} else if (kw(cmd, "OPEN", "open") && argc == 4) {
		resolve_dev(pdata, argv[1]);
		open_dev(pdata, pdata->dev, atol(argv[2]), argv[3], false);
	} else if (kw(cmd, "OPEN", "open") && argc == 5 && kw(argv[4], "CYCLIC", "cyclic")) {
		resolve_dev(pdata, argv[1]);
		open_dev(pdata, pdata->dev, atol(argv[2]), argv[3], true);
	} else if (kw(cmd, "CLOSE", "close") && argc == 2) {
		close_dev(pdata, resolve_dev(pdata, argv[1]));
	} else if (kw(cmd, "READ", "read") && argc >= 2) {
		handle_read(pdata, argv, argc);
	} else if (kw(cmd, "WRITE", "write") && argc >= 3) {
		handle_write(pdata, argv, argc);
	} else if (kw(cmd, "READBUF", "readbuf") && argc == 3) {
		rw_dev(pdata, resolve_dev(pdata, argv[1]), atol(argv[2]), false);
	} else if (kw(cmd, "WRITEBUF", "writebuf") && argc == 3) {
		rw_dev(pdata, resolve_dev(pdata, argv[1]), atol(argv[2]), true);
	} else if (kw(cmd, "SETTRIG", "settrig") && argc == 2) {
		set_trigger(pdata, resolve_dev(pdata, argv[1]), NULL);
	} else if (kw(cmd, "SETTRIG", "settrig") && argc == 3) {
		set_trigger(pdata, resolve_dev(pdata, argv[1]), argv[2]);
	} else if (kw(cmd, "GETTRIG", "gettrig") && argc == 2) {
		get_trigger(pdata, resolve_dev(pdata, argv[1]));
	} else if (kw(cmd, "SET", "set") && argc == 4 &&
			kw(argv[2], "BUFFERS_COUNT", "buffers_count")) {
		set_buffers_count(pdata, resolve_dev(pdata, argv[1]), atol(argv[3]));
	} else {
		reply_einval(pdata);
	}
}

/*
 * Differs from ascii_interpreter() in ops.c: the flex/bison parse loop is
 * replaced by read_line() + dispatch_line(), and the trailing per-device
 * cleanup loop (close_dev_helper() over every device) is omitted. That loop
 * tears down the POSIX RW threads, which do not exist here; no device can be
 * open yet, so there is nothing to tear down.
 */
void ascii_interpreter(struct parser_pdata *pdata)
{
	char line[ASCII_CMD_LINE_MAX];

	while (!pdata->stop && !pdata->binary) {
		ssize_t n = read_line(pdata, line, sizeof(line) - 1);
		if (n <= 0) {
			pdata->stop = true;
			break;
		}

		/* read_line() stores the terminating newline; strip it (and an
		 * optional preceding CR, matching the lexer's "\r?\n" rule). */
		if (line[n - 1] == '\n')
			n--;
		if (n > 0 && line[n - 1] == '\r')
			n--;
		line[n] = '\0';

		dispatch_line(pdata, line);
	}
}

#endif /* WITH_IIOD_V0_COMPAT_GRAMMAR_PARSER */
