#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "byte-buffer.h"
#include "helpers.h"
#include "ut-wrapper.h"

#define BoundsCheck(bytes,index) if (index < 0 || index >= bytes->buffer_len) ErrorExit("Index out of bounds", -1);
#define IntervalCheck(bytes, offset, count) if (offset < 0L || offset + count > bytes->buffer_len) ErrorExit("Index out of bounds", -1);

unsigned char Byte(const bytes from, rsize_t index)
{
	BoundsCheck(from, index);
	return from->buffer[index];
}

uint64_t ReadNumber(const bytes from, rsize_t offset, rsize_t count)
{
	IntervalCheck(from, offset, count);
	return ParseUnsigned(from->buffer + offset, count);
}

int64_t ReadSignedNumber(const bytes from, rsize_t offset, rsize_t count)
{
	IntervalCheck(from, offset, count);
	return ParseSigned(from->buffer + offset, count);
}

bool EqualsBuffer(const bytes first, rsize_t offset, const unsigned char* second, rsize_t count)
{
	IntervalCheck(first, offset, count);
	for (rsize_t i = 0; i < count; ++i)
		if (first->buffer[offset + i] != second[i])
			return false;

	return true;
}

bool Same(const bytes buf, rsize_t offset, unsigned char value, rsize_t count)
{
	BoundsCheck(buf, offset + count);
	for (rsize_t i = 0; i < count; ++i)
		if (buf->buffer[offset + i] != value)
			return false;

	return true;
}

bool Equals(const bytes first, rsize_t offset1, const bytes second, rsize_t offset2, rsize_t count)
{
	IntervalCheck(first, offset1, count);
	IntervalCheck(second, offset2, count);

	for (rsize_t i = 0; i < count; ++i)
		if (first->buffer[offset1 + i] != second->buffer[offset2 + i])
			return false;

	return true;
}

void Patch(bytes dest, rsize_t offset1, const bytes second, rsize_t offset2, rsize_t count)
{
	IntervalCheck(dest, offset1, count);
	IntervalCheck(second, offset2, count);

	for (rsize_t i = 0; i < count; ++i)
		dest->buffer[offset1 + i] = second->buffer[offset2 + i];
}


wchar_t* ToString(const bytes buf)
{
	if (buf->buffer_len == 0)
	{
		SafeCreate(empty, wchar_t*);
		*empty = 0;
		return empty;
	}

	SafePtrBlock(result, wchar_t*, buf->buffer_len * 5);
	swprintf(result, 5, L"%2.2x", *(buf->buffer));
	for (rsize_t i = 1; i < buf->buffer_len; ++i)
	{
		swprintf(result + (i * 3) - 1, 4, L":%2.2x", *(buf->buffer + i));
	}
	return result;
}

void WriteToFile(FILE* f, const bytes buf)
{
	wchar_t* dump = ToString(buf);
	fwprintf(f,L"\n%s\n", dump);
	free(dump);
}

bytes CopyBuffer(const bytes buf)
{
	bytes result = CreateBytes(buf->buffer_len);
	if (!result)
		return NULL;
	memcpy(result->buffer, buf->buffer, buf->buffer_len);
	return result;
}

bytes TakeBufferSlice(const bytes buf, rsize_t offset, rsize_t count)
{
	IntervalCheck(buf, offset, count);
	bytes result = CreateBytes(count);
	if (!result)
		return NULL;

	memcpy(result->buffer, buf->buffer + offset, count);
	return result;
}


bytes CreateBytes(rsize_t length)
{
	unsigned char* buffer = calloc(length, 1);
	if (!buffer)
		ErrorExit("Memory allocation error", -1);

	//bytes result;
	SafeCreate(result, bytes);

	result->buffer_len = length;
	result->buffer = buffer;
	return result;
}

bytes FromBuffer(const void* src, rsize_t length)
{
	bytes result = CreateBytes(length);
	if (!result)
		return NULL;
	memcpy(result->buffer, src, length);
	return result;
}

void Append(bytes first, const bytes second, rsize_t offset, rsize_t count)
{
	AppendAt(first, first->buffer_len, second, offset, count);
}

bool Reserve(bytes buf, rsize_t count)
{
	if (buf->buffer_len < count)
	{
		uint8_t* new_buf = realloc(buf->buffer, count);
		if (new_buf == NULL)
		{
			ErrorExit("Memory Allocation Problem", -1);
			return false;
		}
		buf->buffer = new_buf;
		buf->buffer_len = count;
	}
	return true;
}

void AppendAt(bytes first, rsize_t offset1, const bytes second, rsize_t offset2, rsize_t count)
{
	IntervalCheck(first, offset1, 0);
	IntervalCheck(second, offset2, count);
	unsigned char* newBuffer = realloc(first->buffer, offset1 + count);
	if (newBuffer)
	{
		memcpy(newBuffer + offset1, second->buffer + offset2, count);
		first->buffer = newBuffer;
		first->buffer_len = offset1 + count;
	}
}

void RightTrim(bytes buf, size_t count)
{
	if (count < 0 || count > buf->buffer_len)
		ErrorExit("Index out of bounds", -1);

	buf->buffer_len -= count;
}


bytes CreateEmpty()
{
	SafeCreate(result, bytes);
	result->buffer = NULL;
	result->buffer_len = 0;
	return result;
}

void DeleteBytes(bytes buf)
{
	free(buf->buffer);
	free(buf);
}

void CleanUpBytes(bytes buf)
{
	free(buf->buffer);
}

void CopyBytes(void* dst, const void *src)
{
	((bytes)dst)->buffer = ((bytes)src)->buffer;
	((bytes)dst)->buffer_len = ((bytes)src)->buffer_len;
}


UT_icd buf_icd = {sizeof(struct _bytes), NULL, CopyBytes, CleanUpBytes };

UT_array* CreateBufferList()
{
	UT_array* result;
	utarray_new(result, &buf_icd);
	return result;
}

void AppendBuffer(UT_array* bufferList, const bytes src, rsize_t offset, rsize_t count)
{
	bytes clone = TakeBufferSlice(src, offset, count);
	utarray_push_back(bufferList, clone);
	//free the clone but not the buffer (!):
	free(clone);
}