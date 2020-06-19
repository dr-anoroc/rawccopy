#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include "helpers.h"

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#define SafeStrLen(s,len) do { \
	if ((len = strnlen_s(s, MAX_NTFS_PATH)) == MAX_NTFS_PATH) \
	{														\
		ErrorExit("String exceeded maximum length.", -1);	\
		return NULL;										\
	}} while (0);

#define SafeStrLenW(s,len) do { \
	if ((len = wcsnlen_s(s, MAX_NTFS_PATH)) == MAX_NTFS_PATH) \
	{														\
		ErrorExit("String exceeded maximum length.", -1);	\
		return NULL;										\
	}} while (0);

#define LZNT1_BLOCK_LEN( header ) ( ( (header) & 0x0fff ) + 1 )

#define LZNT1_BLOCK_COMPRESSED( header ) ( (header) & 0x8000 )

#define LZNT1_VALUE_LEN( tuple, split ) \
	( ( ((uint64_t) tuple) & ( ( 1ULL << (split) ) - 1ULL ) ) + 3 )

#define LZNT1_VALUE_OFFSET( tuple, split ) ( ( ((uint64_t)tuple) >> split ) + 1 )


wchar_t* AllocateString(size_t len);
long lznt1_block(bytes compressed, size_t limit, size_t offset1, bytes destination, size_t offset2);


bool LZNT1Decompress(const bytes compressed, bytes decompressed)
{
	const uint16_t* header;
	const uint8_t* end;
	size_t offset = 0;
	size_t out_len = 0;
	size_t block_len;
	size_t limit;
	long block_out_len;

	while (offset != compressed->buffer_len)
	{
		if ((offset + sizeof(*end)) == compressed->buffer_len)
		{
			end = (compressed->buffer + offset);
			if (*end == 0)
				break;
		}

		if ((offset + sizeof(*header)) > compressed->buffer_len)
			return false;

		header = (uint16_t *)(compressed->buffer + offset);

		if (*header == 0)
			break;

		offset += sizeof(*header);

		block_len = LZNT1_BLOCK_LEN(*(size_t*)header);
		if (LZNT1_BLOCK_COMPRESSED(*header))
		{
			limit = (offset + block_len);
			block_out_len = lznt1_block(compressed, limit, offset, decompressed, out_len);
			if (block_out_len < 0)
				return false;
			offset += block_len;
			out_len += block_out_len;
		}
		else if ((offset + block_len) > compressed->buffer_len)
			return false;
		else
		{
			Patch(decompressed, out_len, compressed, offset, block_len);
			offset += block_len;
			out_len += block_len;
		}
	}
	RightTrim(decompressed, decompressed->buffer_len - out_len);
	return true;
}

long lznt1_block(bytes compressed, size_t limit, size_t offset1, bytes destination, size_t offset2)
{
	uint16_t* tuple;
	size_t copy_len;
	size_t block_out_len = 0;
	unsigned int split = 12;
	unsigned int next_threshold = 16;
	unsigned int tag_bit = 0;
	unsigned int tag = 0;

	while (offset1 != limit)
	{
		if (tag_bit == 0)
		{
			tag = *((uint8_t*)(compressed->buffer + offset1));
			offset1++;
			if (offset1 == limit)
				break;
		}

		if (tag & 1)
		{
			if (offset1 + sizeof(*tuple) > limit)
				return -1;

			tuple = (uint16_t*)(compressed->buffer + offset1);
			offset1 += sizeof(*tuple);
			copy_len = LZNT1_VALUE_LEN(*(size_t*)tuple, split);

			Patch(destination, offset2 + block_out_len, destination,
				offset2 + block_out_len - LZNT1_VALUE_OFFSET(*tuple, split), copy_len);

			block_out_len += copy_len;

		}
		else
		{
			Patch(destination, offset2 + block_out_len++, compressed, offset1++, 1);
		}

		while (block_out_len > next_threshold)
		{
			split--;
			next_threshold <<= 1;
		}
		tag >>= 1;
		tag_bit = ((tag_bit + 1) % 8);
	}

	return (long)block_out_len;
}


uint64_t ParseUnsigned(const void* buf, rsize_t length)
{
	uint64_t result = 0;
	memcpy(&result, buf, min(8, length));
	return result;
}

int64_t ParseSigned(const void* buf, rsize_t length)
{
	int64_t result = (length > 0 && *((uint8_t*)buf + length - 1) & 0x80) ? 0xFFFFFFFFFFFFFFFF : 0;
	memcpy(&result, buf, min(8, length));

	return result;

}

void ErrorExit(const char* message, int exit_code)
{
	printf("%.80s\n",message);
	exit(exit_code);
}

void* ErrorCleanUp(const void (*cleanup)(void*), void* object, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	if (cleanup)
		(*cleanup)(object);
	return NULL;
}

int strncmp_nocase(const char* first, const char* second, size_t max)
{
	if (!first)
		return !second;
	else if (!second)
		return 1;
	else
		for (size_t i = 0; i < max; ++i)
		{
			int delta = tolower(first[i]) - tolower(second[i]);
			if (delta != 0 || !first[i])
				return delta;
		}
	return 0;
}


bool LazyParse(const char* source, ...)
{
	va_list args;
	va_start(args, source);
	for (char* token = va_arg(args, char*); token && *token; token = va_arg(args, char*))
	{
		char **result = va_arg(args, char**);
		*(result) = strstr(source, token);
		if (!(*result))
		{
			va_end(args);
			return false;
		}
		source++;
	}
	va_end(args);
	return true;
}

bool IsDigits(const char* input)
{
	if (!input || !*input)
		return false;
	for (int i = 0; i < MAX_NTFS_PATH && input[i]; ++i)
		if (!isdigit(input[i]))
			return false;

	return true;
}

bool IsDigitsW(const wchar_t* input)
{
	if (!input || !*input)
		return false;
	for (int i = 0; i < MAX_NTFS_PATH && input[i]; ++i)
		if (!isdigit(input[i]))
			return false;

	return true;
}

int wstrncmp_nocase(const wchar_t* first, const wchar_t* second, size_t max)
{
	if (!first)
		return !second;
	else if (!second)
		return 1;
	else
		for (size_t i = 0; i < max; ++i)
		{
			int delta = towlower(first[i]) - towlower(second[i]);
			if (delta != 0 || !first[i])
				return delta;
		}
	return 0;
}

wchar_t* ToWideString(const char* str, size_t length)
{
	rsize_t len;
	SafeStrLen(str,len);
	if (length > 0 && len > length)
		len = length;
	wchar_t* result = AllocateString(len);
	rsize_t res;
	mbstowcs_s(&res, result, len + 1, str, len);
	return result;
}

wchar_t* AllocateString(size_t len)
{
	if (len >= MAX_NTFS_PATH)
		return ErrorCleanUp(NULL, NULL, "String exceeds maximum length.\n");

	SafePtrBlock(result, wchar_t*, len + 1);
	return result;
}

wchar_t* JoinStrings(const wchar_t* seperator, ...)
{
	va_list args;
	va_start(args, seperator);
	size_t length = 0;
	size_t part_len = 0;
	size_t sep_len;
	SafeStrLenW(seperator, sep_len);

	for (wchar_t* part = va_arg(args, wchar_t*); part; part = va_arg(args, wchar_t*))
	{
		SafeStrLenW(part, part_len);
		length += length == 0 ? part_len : (part_len ? part_len + sep_len : 0);
		if (length >= MAX_NTFS_PATH)
			return ErrorCleanUp(NULL, NULL, "String exceeds maximum length.\n");
	}
	va_end(args);

	wchar_t* result = AllocateString(length);
	*result = 0;
	va_start(args, seperator);

	for (wchar_t* part = va_arg(args, wchar_t*); part; part = va_arg(args, wchar_t*))
	{
		if (*part)
		{
			if (*result)
				wcscat_s(result, length + 1, seperator);
			wcscat_s(result, length + 1, part);
		}
	}
	return result;
}

wchar_t* NumberToString(uint64_t nmbr, int base)
{
	if (base < 2 || base > 36)
	{
		printf("Invalid base for number conversion.\n");
		return NULL;
	}
	wchar_t* result = AllocateString(_MAX_U64TOSTR_BASE10_COUNT);

	wchar_t* ptr = result, *ptr1 = result, tmp_char;
	uint64_t tmp_value;

	do {
		tmp_value = nmbr;
		nmbr /= base;
		*ptr++ = L"zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + (tmp_value - nmbr * base)];
	} while (nmbr);

	*ptr-- = L'\0';
	while (ptr1 < ptr) {
		tmp_char = *ptr;
		*ptr-- = *ptr1;
		*ptr1++ = tmp_char;
	}
	return result;
}

wchar_t* NtfsNameExtract(const uint8_t *input, rsize_t length)
{
	wchar_t* result = AllocateString(length);
	if (result != NULL)
	{
		memcpy(result, input, length*sizeof(wchar_t));
		*(result + length) = 0;
	}
	return result;
}

wchar_t* FormatNTFSDate(uint64_t date)
{
	SYSTEMTIME system_tm;
	if (!FileTimeToSystemTime((FILETIME *)&date, &system_tm))
		return NULL;

	SYSTEMTIME loc_system_tm;
	if (!SystemTimeToTzSpecificLocalTime(NULL, &system_tm, &loc_system_tm))
		return NULL;

	wchar_t* result = AllocateString(21);
	int date_ln = GetDateFormatW(LOCALE_USER_DEFAULT, 0, &loc_system_tm, L"dd'-'MMM'-'yyyy ", result, 13);

	GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &loc_system_tm, L"HH':'mm':'ss", result + date_ln - 1, 9);

	return result;
}


wchar_t* DuplicateString(const wchar_t* src)
{
	size_t len;
	SafeStrLenW(src,len);
	wchar_t* result = AllocateString(len);
	if (result)
	{
		memcpy(result, src, (len + 1) * sizeof(wchar_t));
	}
	return result;
}
