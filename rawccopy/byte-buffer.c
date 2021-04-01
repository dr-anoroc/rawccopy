#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "byte-buffer.h"
#include "helpers.h"
#include "ut-wrapper.h"

#define BoundsCheck(bytes,index) if (index < 0 || index >= bytes->buffer_len) ErrorExit("Index out of bounds", -1);
#define IntervalCheck(bytes, offset, count) if (offset < 0L || offset + count > bytes->buffer_len) ErrorExit("Index out of bounds", -1);

bool Equals(const bytes first, rsize_t offset1, const bytes second, rsize_t offset2, rsize_t count)
{
	IntervalCheck(first, offset1, count);
	IntervalCheck(second, offset2, count);

	return !memcmp(first->buffer + offset1, second->buffer + offset2, count);
}

void Patch(bytes dest, rsize_t offset1, const bytes second, rsize_t offset2, rsize_t count)
{
	IntervalCheck(dest, offset1, count);
	IntervalCheck(second, offset2, count);

	memmove(dest->buffer + offset1, second->buffer + offset2, count);
}


void SetBytes(bytes dest, uint8_t val, rsize_t offset, rsize_t count)
{
	Reserve(dest, offset + count);
	memset(dest->buffer + offset, val, count);
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

//Get's passed a pointer to a 'bytes' buffer, so it needs to dereference it before
//calling DeleteBytes
void DeleteBytesPtr(void* buf)
{
	DeleteBytes(*(bytes*)buf);
}


//UT_ICD for a list of POINTERS to bytes buffers,
const static UT_icd byte_lst_icd = { sizeof(void*), NULL, NULL, DeleteBytesPtr };

UT_array* ListOfBuffers()
{
	UT_array* result;
	utarray_new(result, &byte_lst_icd);
	return result;
}
