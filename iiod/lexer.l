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

#include "parser.h"
%}

%option noyywrap reentrant bison-bridge nounistd

PRINT PRINT|print
READ READ|read
WRITE WRITE|write
READBUF READBUF|readbuf
EXIT EXIT|QUIT|exit|quit
HELP HELP|help

END [ \t]*\n
SPACE [ \t]+
WORD (iio:)?(-|_|[[:alnum:]])+

%%

{PRINT} {
	return PRINT;
}

{READ} {
	return READ;
}

{READBUF} {
	return READBUF;
}

{WRITE} {
	return WRITE;
}

{EXIT} {
	return EXIT;
}

{HELP} {
	return HELP;
}

{WORD} {
	yylval->word = strdup(yytext);
	return WORD;
}

{SPACE} {
	return SPACE;
}

{END} {
	return END;
}

. ;
