#ifndef HELPERS_H
#define HELPERS_H

#define MAX_LEN 520
#define MAX_NTFS_PATH 0x1000

#include <stdbool.h>
#include <stdarg.h>

#include "byte-buffer.h"


#define SafeAlloc(ptr, cnt) if ((ptr = malloc((cnt)*sizeof(*ptr))) == NULL) {ErrorExit("Memory allocation problem.", -1);return NULL;}
#define SafeCreate(ptr, ptrT) ptrT ptr; SafeAlloc(ptr,1)
#define SafePtrBlock(ptr, ptrT, cnt) ptrT ptr; SafeAlloc(ptr, cnt);

#define StringAssign(oldV,newV) do {void* calcV = (newV);if (oldV != calcV){ if (oldV){ free(oldV);} oldV = calcV; } } while(0)

bool LZNT1Decompress(const bytes compressed, bytes decompressed);

uint64_t ParseUnsigned(const void* buf, rsize_t length);

int64_t ParseSigned(const void* buf, rsize_t length);

bool LazyParse(const char * source, ...);

void ErrorExit(const char* message, int exit_code);

void* ErrorCleanUp(const void (*cleanup)(void *), void* object, const char* format, ...);

int strncmp_nocase(const char* first, const char* second, size_t max);

int wstrncmp_nocase(const wchar_t* first, const wchar_t* second, size_t max);

bool IsDigits(const char* input);

bool IsDigitsW(const wchar_t* input);

wchar_t* NtfsNameExtract(const uint8_t * input, rsize_t length);

wchar_t* ToWideString(const char* str, size_t length);

wchar_t* JoinStrings(const wchar_t* seperator, ...);

wchar_t* DuplicateString(const wchar_t* src);

wchar_t* NumberToString(uint64_t nmbr, int base);

wchar_t* FormatNTFSDate(uint64_t date);

#endif
