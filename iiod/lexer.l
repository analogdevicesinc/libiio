%{
// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "parser.h"
#include "ops.h"

#include <string.h>
%}

%option noyywrap reentrant bison-bridge nounistd nounput noinput

WORD (([[:alpha:]]+,)|(iio:))?(-|_|\.|[[:alnum:]])+

%s WANT_DEVICE
%s WANT_CHN_OR_ATTR
%s WANT_CHN
%s WANT_ATTR
%s WANT_VALUE
%%

<INITIAL>VERSION|version {
	return VERSION;
}

<INITIAL>PRINT|print {
	return PRINT;
}

<INITIAL>ZPRINT|zprint {
	return ZPRINT;
}

<INITIAL>EXIT|exit|QUIT|quit {
	return EXIT;
}

<INITIAL>HELP|help {
	return HELP;
}

<INITIAL>TIMEOUT|timeout {
	return TIMEOUT;
}

<INITIAL>OPEN|open {
	BEGIN(WANT_DEVICE);
	return OPEN;
}

<INITIAL>CLOSE|close {
	BEGIN(WANT_DEVICE);
	return CLOSE;
}

<INITIAL>READ|read {
	BEGIN(WANT_DEVICE);
	return READ;
}

<INITIAL>READBUF|readbuf {
	BEGIN(WANT_DEVICE);
	return READBUF;
}

<INITIAL>WRITEBUF|writebuf {
	BEGIN(WANT_DEVICE);
	return WRITEBUF;
}

<INITIAL>WRITE|write {
	BEGIN(WANT_DEVICE);
	return WRITE;
}

<INITIAL>SETTRIG|settrig {
	BEGIN(WANT_DEVICE);
	return SETTRIG;
}

<INITIAL>GETTRIG|gettrig {
	BEGIN(WANT_DEVICE);
	return GETTRIG;
}

<INITIAL>SET|set {
	BEGIN(WANT_DEVICE);
	return SET;
}

<WANT_DEVICE>{WORD} {
	struct parser_pdata *pdata = yyget_extra(yyscanner);
	struct iio_device *dev = iio_context_find_device(pdata->ctx, yytext);
	yylval->dev = dev;
	pdata->dev = dev;
	BEGIN(WANT_CHN_OR_ATTR);
	return DEVICE;
}

<WANT_CHN_OR_ATTR>BUFFERS_COUNT|buffers_count {
	BEGIN(WANT_VALUE);
	return BUFFERS_COUNT;
}

<WANT_CHN_OR_ATTR>DEBUG|debug {
	BEGIN(WANT_ATTR);
	return DEBUG_ATTR;
}

<WANT_CHN_OR_ATTR>BUFFER|buffer {
	BEGIN(WANT_ATTR);
	return BUFFER_ATTR;
}

<WANT_CHN_OR_ATTR>INPUT|input|OUTPUT|output {
	struct parser_pdata *pdata = yyget_extra(yyscanner);
	pdata->channel_is_output = yytext[0] == 'o' || yytext[0] == 'O';
	BEGIN(WANT_CHN);
	return IN_OUT;
}

<WANT_CHN>{WORD} {
	struct parser_pdata *pdata = yyget_extra(yyscanner);
	struct iio_channel *chn = NULL;
	if (pdata->dev)
		chn = iio_device_find_channel(pdata->dev,
					yytext, pdata->channel_is_output);
	yylval->chn = chn;
	pdata->chn = chn;
	BEGIN(WANT_ATTR);
	return CHANNEL;
}

<WANT_VALUE>{WORD} {
	char *end;
	char errstr[100];

	errno = 0;
	yylval->value = strtol(yytext, &end, 10);
	if (yytext == end || errno == ERANGE) {
		snprintf(errstr, sizeof(errstr), "lex : bad long constant: %s",(char*)yytext);
		perror(errstr);
	}
	return VALUE;
}

CYCLIC|cyclic {
	return CYCLIC;
}

{WORD} {
	yylval->word = strdup(yytext);
	return WORD;
}

[ \t]+ {
	return SPACE;
}

[ \t]*\r?\n {
	BEGIN(INITIAL);
	return END;
}

. {
	BEGIN(INITIAL);
}
