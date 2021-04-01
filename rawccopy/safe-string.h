#ifndef SAFE_STRING_H
#define SAFE_STRING_H

#include <stdarg.h>
#include <string.h>
#include "byte-buffer.h"


typedef bytes string;

#define NewString() (string)FromBuffer(L"\0", sizeof(wchar_t))

#define DeleteString(str) DeleteBytes(str)

#define CopyString(str) (string)(CopyBuffer(str))

#define StringLen(str) (((bytes)(str))->buffer_len / sizeof(wchar_t) - 1)

#define BaseString(str) (wchar_t *)(((string)(str))->buffer)

string StringPrint(string str, rsize_t offset, const wchar_t* fmt, ...);

bool StringAppend(string first, rsize_t offset1, string second, rsize_t offset2, rsize_t length);

void ClearString(string str);

#endif //SAFE_STRING_H

