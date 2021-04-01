#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "safe-string.h"
#include "helpers.h"

string StringPrint(string str, rsize_t offset, const wchar_t* fmt, ...)
{
	if (!str)
		str = CreateEmpty();

	if (offset > StringLen(str))
		return ErrorCleanUp(NULL, NULL, "Index out of bounds.\n");

	va_list ap;
	for (rsize_t extra = 0x100; extra < 0x10000; extra <<= 2)
	{
		Reserve(str, (offset + extra + 1) * sizeof(wchar_t));
		va_start(ap, fmt);
		long len = vswprintf((wchar_t*)(str->buffer + offset * sizeof(wchar_t)), extra + 1, fmt, ap);
		if (len >= 0)
		{
			RightTrim(str, str->buffer_len - (offset + len + 1) * sizeof(wchar_t));
			va_end(ap);
			return str;
		}
	}
	va_end(ap);
	return NULL;
}

void ClearString(string str)
{
	RightTrim(str, str->buffer_len - sizeof(wchar_t));
	*((wchar_t*)str->buffer) = L'\0';
}

bool StringAppend(string first, rsize_t offset1, string second, rsize_t offset2, rsize_t length)
{
	if (offset1 > StringLen(first) || offset2 > StringLen(second))
		return CleanUpAndFail(NULL, NULL, "Index out of bounds.\n");

	Reserve(first, (offset1 + 1) * sizeof(wchar_t) + length);

	AppendAt(first, offset1 * sizeof(wchar_t), second, offset1 * sizeof(wchar_t), length * sizeof(wchar_t));

	*(wchar_t*)(first->buffer + first->buffer_len - sizeof(wchar_t)) = L'\0';

	return true;
}
