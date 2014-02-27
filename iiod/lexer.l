%option noyywrap reentrant bison-bridge nounistd

%{
#include "parser.h"
%}

PRINT PRINT|print
READ READ|read
WRITE WRITE|write
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
