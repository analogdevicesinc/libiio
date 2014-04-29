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

#define YYDEBUG 1

#include "../debug.h"

#include <sys/socket.h>

int yylex();
int yylex_init_extra(void *d, yyscan_t *scanner);
int yylex_destroy(yyscan_t yyscanner);

void * yyget_extra(yyscan_t scanner);
void yyset_in(FILE *in, yyscan_t scanner);
void yyset_out(FILE *out, yyscan_t scanner);

#define ECHO send(fileno(yyout), yytext, yyleng, 0)

#define YY_INPUT(buf,result,max_size) { \
		int c = '*'; \
		size_t n; \
		for ( n = 0; n < max_size && \
			     recv(fileno(yyin), &c, 1, 0) && c != '\n'; ++n ) \
			buf[n] = (char) c; \
		if ( c == '\n' ) \
			buf[n++] = (char) c; \
		else if (c == EOF && ferror( yyin ) ) { \
			ERROR( "input in flex scanner failed\n" ); \
			n = 1; \
			buf[0] = '\n'; \
		} \
		result = n; \
	}
}

%define api.pure
%lex-param { yyscan_t scanner }
%parse-param { yyscan_t scanner }

%union {
	char *word;
}

%token SPACE
%token END

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

%token <word> WORD

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
		output(pdata->out, "Available commands:\n\n"
		"\tHELP\n"
		"\t\tPrint this help message\n"
		"\tEXIT\n"
		"\t\tClose the current session\n"
		"\tPRINT\n"
		"\t\tDisplays a XML string corresponding to the current IIO context\n"
		"\tOPEN <device> <samples_count> <mask>\n"
		"\t\tOpen the specified device with the given mask of channels\n"
		"\tCLOSE <device>\n"
		"\t\tClose the specified device\n"
		"\tREAD <device> [\"debug\"|<channel>] <attribute>\n"
		"\t\tRead the value of an attribute\n"
		"\tWRITE <device> [\"debug\"|<channel>] <attribute> <value>\n"
		"\t\tSet the value of an attribute\n"
		"\tREADBUF <device> <bytes_count>\n"
		"\t\tRead raw data from the specified device\n"
		"\tWRITEBUF <device> <bytes_count>\n"
		"\t\tWrite raw data to the specified device\n"
		"\tGETTRIG <device>\n"
		"\t\tGet the name of the trigger used by the specified device\n"
		"\tSETTRIG <device> [<trigger>]\n"
		"\t\tSet the trigger to use for the specified device\n");
		YYACCEPT;
	}
	| PRINT END {
		struct parser_pdata *pdata = yyget_extra(scanner);
		const char *xml = iio_context_get_xml(pdata->ctx);
		if (!pdata->verbose) {
			char buf[128];
			sprintf(buf, "%lu\n", (unsigned long) strlen(xml));
			output(pdata->out, buf);
		}
		output(pdata->out, xml);
		output(pdata->out, "\n");
		YYACCEPT;
	}
	| OPEN SPACE WORD SPACE WORD SPACE WORD END {
		char *id = $3, *nb = $5, *mask = $7;
		struct parser_pdata *pdata = yyget_extra(scanner);
		unsigned long samples_count = atol(nb);
		int ret = open_dev(pdata, id, samples_count, mask);
		free(id);
		free(nb);
		free(mask);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| CLOSE SPACE WORD END {
		char *id = $3;
		struct parser_pdata *pdata = yyget_extra(scanner);
		int ret = close_dev(pdata, id);
		free(id);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| READ SPACE WORD SPACE WORD END {
		char *id = $3, *attr = $5;
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = read_dev_attr(pdata, id, attr, false);
		free(id);
		free(attr);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| READ SPACE WORD SPACE WORD SPACE WORD END {
		char *id = $3, *chn = $5, *attr = $7;
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret;
		if (!strcmp(chn, "debug"))
			ret = read_dev_attr(pdata, id, attr, true);
		else
			ret = read_chn_attr(pdata, id, chn, attr);
		free(id);
		free(chn);
		free(attr);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| READBUF SPACE WORD SPACE WORD END {
		char *id = $3, *attr = $5;
		unsigned long nb = atol(attr);
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = rw_dev(pdata, id, nb, false);

		free(id);
		free(attr);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| WRITEBUF SPACE WORD SPACE WORD END {
		char *id = $3, *attr = $5;
		unsigned long nb = atol(attr);
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = rw_dev(pdata, id, nb, true);

		/* Discard additional data */
		yyclearin;

		free(id);
		free(attr);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| WRITE SPACE WORD SPACE WORD SPACE WORD END {
		char *id = $3, *attr = $5, *value = $7;
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = write_dev_attr(pdata, id, attr, value, false);
		free(id);
		free(attr);
		free(value);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| WRITE SPACE WORD SPACE WORD SPACE WORD SPACE WORD END {
		char *id = $3, *chn = $5, *attr = $7, *value = $9;
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret;
		if (!strcmp(chn, "debug"))
			ret = write_dev_attr(pdata, id, attr, value, true);
		else
			ret = write_chn_attr(pdata, id, chn, attr, value);
		free(id);
		free(chn);
		free(attr);
		free(value);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| SETTRIG SPACE WORD SPACE WORD END {
		char *id = $3, *trig = $5;
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = set_trigger(pdata, id, trig);
		free(id);
		free(trig);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| SETTRIG SPACE WORD END {
		char *id = $3;
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = set_trigger(pdata, id, NULL);
		free(id);
		if (ret < 0)
			YYABORT;
		else
			YYACCEPT;
	}
	| GETTRIG SPACE WORD END {
		char *id = $3;
		struct parser_pdata *pdata = yyget_extra(scanner);
		ssize_t ret = get_trigger(pdata, id);
		free(id);
		if (ret < 0)
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
		output(pdata->out, "ERROR: ");
		output(pdata->out, msg);
		output(pdata->out, "\n");
	} else {
		char buf[128];
		sprintf(buf, "%i\n", -EINVAL);
		output(pdata->out, buf);
	}
}
