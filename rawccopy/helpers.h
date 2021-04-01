#ifndef HELPERS_H
#define HELPERS_H

#include <stdbool.h>
#include <stdarg.h>

#include "byte-buffer.h"
#include "ut-wrapper.h"
#include "safe-string.h"


#define SafeAlloc(ptr, cnt) if ((ptr = malloc((cnt)*sizeof(*ptr))) == NULL) {ErrorExit("Memory allocation problem.", -1);return NULL;}
#define SafeCreate(ptr, ptrT) ptrT ptr; SafeAlloc(ptr,1)
#define SafePtrBlock(ptr, ptrT, cnt) ptrT ptr; SafeAlloc(ptr, cnt);


bool LZNT1Decompress(const bytes compressed, rsize_t offset1, bytes decompressed, rsize_t offset2);

uint64_t ParseUnsigned(const void* buf, rsize_t length);

int64_t ParseSigned(const void* buf, rsize_t length);

void ErrorExit(const char* message, int exit_code);

void* ErrorCleanUp(const void (*cleanup)(void *), void* object, const char* format, ...);

bool CleanUpAndFail(const void (*cleanup)(void*), void* object, const char* format, ...);

int wstrncmp_nocase(const wchar_t* first, const wchar_t* second, size_t max);

bool IsDigits(const char* input);

string ExecutablePath();

int32_t FindInArray(const UT_array * array, const void* new_elem, void* context, int (*comp)(const void* first, const void* second, void* context));

#endif
