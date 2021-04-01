#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include "helpers.h"
#include "safe-string.h"
#include "regex.h"


#define LZNT1_BLOCK_LEN( header ) ( ( (header) & 0x0fff ) + 1 )

#define LZNT1_BLOCK_COMPRESSED( header ) ( (header) & 0x8000 )

#define LZNT1_VALUE_LEN( tuple, split ) \
	( ( ((uint64_t) tuple) & ( ( 1ULL << (split) ) - 1ULL ) ) + 3 )

#define LZNT1_VALUE_OFFSET( tuple, split ) ( ( ((uint64_t)tuple) >> split ) + 1 )


long lznt1_block(bytes compressed, size_t limit, size_t offset1, bytes destination, size_t offset2);

bool LZNT1Decompress(const bytes compressed, rsize_t offset1, bytes decompressed, rsize_t offset2)
{
	const uint16_t* header;
	const uint8_t* end;
	size_t out_len = 0;
	size_t block_len;
	size_t limit;
	long block_out_len;

	while (offset1 != compressed->buffer_len)
	{
		if ((offset1 + sizeof(*end)) == compressed->buffer_len)
		{
			end = (compressed->buffer + offset1);
			if (*end == 0)
				break;
		}

		if ((offset1 + sizeof(*header)) > compressed->buffer_len)
			return false;

		header = (uint16_t *)(compressed->buffer + offset1);

		if (*header == 0)
			break;

		offset1 += sizeof(*header);

		block_len = LZNT1_BLOCK_LEN(*(size_t*)header);
		if (LZNT1_BLOCK_COMPRESSED(*header))
		{
			limit = (offset1 + block_len);
			block_out_len = lznt1_block(compressed, limit, offset1, decompressed, out_len);
			if (block_out_len < 0)
				return false;
			offset1 += block_len;
			out_len += block_out_len;
		}
		else if ((offset1 + block_len) > compressed->buffer_len)
			return false;
		else
		{
			memcpy(decompressed->buffer + out_len, compressed->buffer + offset1, block_len);

			offset1 += block_len;
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

			uint8_t* dst = destination->buffer + offset2 + block_out_len;
			uint8_t* src = dst - LZNT1_VALUE_OFFSET(*tuple, split);
			block_out_len += copy_len;
			while (copy_len--)
				*(dst++) = *(src++);
		}
		else
			*(destination->buffer + offset2 + block_out_len++) = *(compressed->buffer + offset1++);

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

void *ErrorCleanUp(const void (*cleanup)(void*), void* object, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	if (cleanup)
		(*cleanup)(object);
	return NULL;
}

bool CleanUpAndFail(const void (*cleanup)(void*), void* object, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	if (cleanup)
		(*cleanup)(object);
	return false;
}

bool IsDigits(const char* input)
{
	char* end_ptr = NULL;
	return match("^\\d\\d*$", input, &end_ptr);
}

string ExecutablePath()
{
	string result = CreateEmpty();

	for (rsize_t size = 8; size < 0x1000; size <<= 2)
	{
		Reserve(result, (size + 1) * sizeof(wchar_t));
		DWORD cnt = GetModuleFileNameW(NULL, (wchar_t*)result->buffer, (DWORD)size);
		if (cnt < size)
		{
			wchar_t* file_pt = wcsrchr((wchar_t*)result->buffer, L'\\');
			if (file_pt)
			{
				*file_pt = L'\0';
				RightTrim(result, result->buffer_len - ((char*)file_pt - result->buffer));
				return result;
			}
		}

	}
	return ErrorCleanUp(DeleteBytes, result, "");
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


int32_t FindInArray(const UT_array* array, const void* new_elem, void *context, int (*comp)(const void* first, const void* second, void* context))
{
	int32_t l = 0, r, max;
	r = max = utarray_len(array);

	while (l <= r && l < max)
	{
		int32_t m = (l + r) / 2;
		int cmp = comp(new_elem, utarray_eltptr(array, (uint32_t)m), context);
		if (cmp < 0)
			r = m - 1;
		else if (cmp > 0)
			l = m + 1;
		else
			return m;
	}
	return ~l;
}

