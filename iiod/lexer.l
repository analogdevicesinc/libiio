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

%%

VERSION|version {
	return VERSION;
}

PRINT|print {
	return PRINT;
}

OPEN|open {
	return OPEN;
}

CLOSE|close {
	return CLOSE;
}

READ|read {
	return READ;
}

READBUF|readbuf {
	return READBUF;
}

WRITEBUF|writebuf {
	return WRITEBUF;
}

WRITE|write {
	return WRITE;
}

SETTRIG|settrig {
	return SETTRIG;
}

GETTRIG|gettrig {
	return GETTRIG;
}

EXIT|exit|QUIT|quit {
	return EXIT;
}

HELP|help {
	return HELP;
}

(([[:alpha:]]+,)|(iio:))?(-|_|\.|[[:alnum:]])+ {
	yylval->word = strdup(yytext);
	return WORD;
}

[ \t]+ {
	return SPACE;
}

[ \t]*\n {
	return END;
}

. ;
