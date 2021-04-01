#include <stdlib.h>
#include <ctype.h>
#include "regex.h"

int matchhere(const char* regexp, const char* text, char** end);
int tokenmatch(const char* regex_start, int escaped, const char* text_start);
int matchstar(const char* regex_token, const char* regexp_rest, const char* text, char** end);

char* match(const char* regexp, const char* text, char** end)
{
	*end = NULL;
	if (regexp[0] == '^')
		return matchhere(regexp + 1, text, end) ? (char*)text : NULL;
	do {    /* must look even if string is empty */
		if (matchhere(regexp, text, end))
			return (char *)text;
	} while (*text++ != '\0');
	*end = NULL;
	return 0;
}

/* matchhere: search for regexp at beginning of text */
int matchhere(const char* regexp, const char* text, char** end)
{
	//In case we step out here:
	*end = (char*)text;

	if (regexp[0] == '\0')
		return 1;

	char* next_rc = (char*)regexp + 1;
	int esc = 0;
	if (*regexp == '\\' &&
		(*next_rc == '.' || *next_rc == '^' || *next_rc == '$' || *next_rc == '*' || *next_rc == 'd'))
	{
		next_rc++;
		esc = 1;
	}

	if (*next_rc == '*')
		return matchstar(regexp, next_rc + 1, text, end);

	if (regexp[0] == '$' && regexp[1] == '\0')
	{
		if (*text == '\0')
			return 1;
		else
		{
			*end = NULL;
			return 0;
		}
	}

	if (*text != '\0' && tokenmatch(regexp, esc, text))
		return matchhere(next_rc, text + 1, end);

	*end = NULL;
	return 0;
}

int tokenmatch(const char* regex_start, int escaped, const char* text_start)
{
	if (*regex_start == '.')
		return 1;
	if (!escaped || *(++regex_start) != 'd')
		return tolower(*regex_start) == tolower(*text_start);

	return isdigit(*text_start);	
}

/* matchstar: search for c*regexp at beginning of text */
int matchstar(const char* regex_token, const char* regexp_rest, const char* text, char** end)
{
	int escaped = (int)(regexp_rest - regex_token - 2);
	do {    /* a * matches zero or more instances */
		if (matchhere(regexp_rest, text, end))
			return 1;
	} while (*text != '\0' && tokenmatch(regex_token, escaped, text++));
	*end = NULL;
	return 0;
}

