%{
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

#include "ops.h"
#include "parser.h"

#include <errno.h>
#include <string.h>

void yyerror(yyscan_t scanner, const char *msg);
%}

%code requires {
#ifndef YY_TYPEDEF_YY_SCANNER_T
#define YY_TYPEDEF_YY_SCANNER_T
typedef void *yyscan_t;
#endif

#include "../iio-config.h"
#include "../debug.h"

#include <stdbool.h>
#include <sys/socket.h>

int yylex();
int yylex_init_extra(void *d, yyscan_t *scanner);
int yylex_destroy(yyscan_t yyscanner);

void * yyget_extra(yyscan_t scanner);
ssize_t yy_input(yyscan_t scanner, char *buf, size_t max_size);

#define ECHO do { \
		struct parser_pdata *pdata = yyget_extra(yyscanner); \
		write_all(pdata, yytext, yyleng); \
	} while (0)

#define YY_INPUT(buf,result,max_size) do { \
		ssize_t res = yy_input(yyscanner, buf, max_size); \
		result = res <= 0 ? YY_NULL : (size_t) res; \
	} while (0)
}

%define api.pure
%lex-param { yyscan_t scanner }
%parse-param { yyscan_t scanner }

%union {
	char *word;
	struct iio_device *dev;
	struct iio_channel *chn;
	long value;
}

%token SPACE
%token END

%token VERSION
%token EXIT
%token HELP
%token OPEN
%token CLOSE
%token PRINT
%token READ
%token READBUF
%token WRITEBUF
%token WRITE
%token SETTRIG
%token GETTRIG
%token TIMEOUT
%token DEBUG_ATTR
%token IN_OUT
%token CYCLIC
%token SET
%token BUFFERS_COUNT

%token <word> WORD
%token <dev> DEVICE
%token <chn> CHANNEL
%token <value> VALUE;

%destructor { DEBUG("Freeing token \"%s\"\n", $$); free($$); } <word>

%start Line
%%

Line:
	END {
		YYACCEPT;
	}
	| EXIT END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		pdata->stop = true;
		YYACCEPT;
	}
	| HELP END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		output(pdata, "Available commands:\n\n"
		"\tHELP\n"
		"\t\tPrint this help message\n"
		"\tEXIT\n"
		"\t\tClose the current session\n"
		"\tPRINT\n"
		"\t\tDisplays a XML string corresponding to the current IIO context\n"
		"\tVERSION\n"
		"\t\tGet the version of libiio in use\n"
		"\tTIMEOUT <timeout_ms>\n"
		"\t\tSet the timeout (in ms) for I/O operations\n"
		"\tOPEN <device> <samples_count> <mask> [CYCLIC]\n"
		"\t\tOpen the specified device with the given mask of channels\n"
		"\tCLOSE <device>\n"
		"\t\tClose the specified device\n"
		"\tREAD <device> DEBUG|[INPUT|OUTPUT <channel>] [<attribute>]\n"
		"\t\tRead the value of an attribute\n"
		"\tWRITE <device> DEBUG|[INPUT|OUTPUT <channel>] [<attribute>] <bytes_count>\n"
		"\t\tSet the value of an attribute\n"
		"\tREADBUF <device> <bytes_count>\n"
		"\t\tRead raw data from the specified device\n"
		"\tWRITEBUF <device> <bytes_count>\n"
		"\t\tWrite raw data to the specified device\n"
		"\tGETTRIG <device>\n"
		"\t\tGet the name of the trigger used by the specified device\n"
		"\tSETTRIG <device> [<trigger>]\n"
		"\t\tSet the trigger to use for the specified device\n"
		"\tSET <device> BUFFERS_COUNT <count>\n"
		"\t\tSet the number of kernel buffers for the specified device\n");
		YYACCEPT;
	}
	| VERSION END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		char buf[128];
		snprintf(buf, sizeof(buf), "%u.%u.%-7.7s\n", LIBIIO_VERSION_MAJOR,
						LIBIIO_VERSION_MINOR, LIBIIO_VERSION_GIT);
		output(pdata, buf);
		YYACCEPT;
	}
	| PRINT END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		const char *xml = iio_context_get_xml(pdata->ctx);
		if (!pdata->verbose) {
			char buf[128];
			sprintf(buf, "%lu\n", (unsigned long) strlen(xml));
			output(pdata, buf);
		}
		output(pdata, xml);
		output(pdata, "\n");
		YYACCEPT;
	}
	| TIMEOUT SPACE WORD END {
		char *word = $3;
		struct parser_pdata *pdata = yyget_extra(scanner);
		unsigned int timeout = (unsigned int) atoi(word);
		int ret = set_timeout(pdata, timeout);
		free(word);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| OPEN SPACE DEVICE SPACE WORD SPACE WORD SPACE CYCLIC END {
		char *nb = $5, *mask = $7;
		struct parser_pdata *pdata = yyget_extra(scanner);
		unsigned long samples_count = atol(nb);
		int ret = open_dev(pdata, $3, samples_count, mask, true);
		free(nb);
		free(mask);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| OPEN SPACE DEVICE SPACE WORD SPACE WORD END {
		char *nb = $5, *mask = $7;
		struct parser_pdata *pdata = yyget_extra(scanner);
		unsigned long samples_count = atol(nb);
		int ret = open_dev(pdata, $3, samples_count, mask, false);
		free(nb);
		free(mask);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| CLOSE SPACE DEVICE END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		int ret = close_dev(pdata, $3);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| READ SPACE DEVICE END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		if (read_dev_attr(pdata, $3, NULL, false) < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| READ SPACE DEVICE SPACE WORD END {
		char *attr = $5;
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = read_dev_attr(pdata, $3, attr, false);
		free(attr);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| READ SPACE DEVICE SPACE DEBUG_ATTR END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		if (read_dev_attr(pdata, $3, NULL, true) < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| READ SPACE DEVICE SPACE DEBUG_ATTR SPACE WORD END {
		char *attr = $7;
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = read_dev_attr(pdata, $3, attr, true);
		free(attr);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| READ SPACE DEVICE SPACE IN_OUT SPACE CHANNEL END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		if (read_chn_attr(pdata, $7, NULL) < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| READ SPACE DEVICE SPACE IN_OUT SPACE CHANNEL SPACE WORD END {
		char *attr = $9;
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = read_chn_attr(pdata, $7, attr);
		free(attr);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| READBUF SPACE DEVICE SPACE WORD END {
		char *len = $5;
		unsigned long nb = atol(len);
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = rw_dev(pdata, $3, nb, false);
		free(len);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| WRITEBUF SPACE DEVICE SPACE WORD END {
		char *len = $5;
		unsigned long nb = atol(len);
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = rw_dev(pdata, $3, nb, true);

		/* Discard additional data */
		yyclearin;

		free(len);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| WRITE SPACE DEVICE SPACE WORD END {
		char *len = $5;
		unsigned long nb = atol(len);
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = write_dev_attr(pdata, $3, NULL, nb, false);
		free(len);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| WRITE SPACE DEVICE SPACE WORD SPACE WORD END {
		char *attr = $5, *len = $7;
		unsigned long nb = atol(len);
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = write_dev_attr(pdata, $3, attr, nb, false);
		free(attr);
		free(len);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| WRITE SPACE DEVICE SPACE DEBUG_ATTR SPACE WORD END {
		char *len = $7;
		unsigned long nb = atol(len);
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = write_dev_attr(pdata, $3, NULL, nb, true);
		free(len);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| WRITE SPACE DEVICE SPACE DEBUG_ATTR SPACE WORD SPACE WORD END {
		char *attr = $7, *len = $9;
		unsigned long nb = atol(len);
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = write_dev_attr(pdata, $3, attr, nb, true);
		free(attr);
		free(len);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| WRITE SPACE DEVICE SPACE IN_OUT SPACE CHANNEL SPACE WORD END {
		char *len = $9;
		unsigned long nb = atol(len);
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = write_chn_attr(pdata, $7, NULL, nb);
		free(len);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| WRITE SPACE DEVICE SPACE IN_OUT SPACE CHANNEL SPACE WORD SPACE WORD END {
		char *attr = $9, *len = $11;
		unsigned long nb = atol(len);
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = write_chn_attr(pdata, $7, attr, nb);
		free(attr);
		free(len);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| SETTRIG SPACE DEVICE SPACE WORD END {
		char *trig = $5;
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = set_trigger(pdata, $3, trig);
		free(trig);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| SETTRIG SPACE DEVICE END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		if (set_trigger(pdata, $3, NULL) < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| GETTRIG SPACE DEVICE END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		if (get_trigger(pdata, $3) < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| SET SPACE DEVICE SPACE BUFFERS_COUNT SPACE VALUE END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		if (set_buffers_count(pdata, $3, $7) < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| error END {
		yyclearin;
		yyerrok;
		YYACCEPT;
	}
	;

%%

void yyerror(yyscan_t scanner, const char *msg)
{
	struct parser_pdata *pdata = yyget_extra(scanner);
	if (pdata->verbose) {
		output(pdata, "ERROR: ");
		output(pdata, msg);
		output(pdata, "\n");
	} else {
		char buf[128];
		sprintf(buf, "%i\n", -EINVAL);
		output(pdata, buf);
	}
}

ssize_t yy_input(yyscan_t scanner, char *buf, size_t max_size)
{
	struct parser_pdata *pdata = yyget_extra(scanner);
	ssize_t ret;

	ret = read_line(pdata, buf, max_size);
	if (ret < 0)
		return ret;
	if (ret == 0)
		return -EIO;

	if ((size_t) ret == max_size)
		buf[max_size - 1] = '\0';

	return ret;
}
