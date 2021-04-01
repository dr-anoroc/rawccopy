#ifndef BYTE_BUFFER_H
#define BYTE_BUFFER_H

#include "ut-wrapper.h"

#include <stdint.h>

#include <stdbool.h>
#include <stdio.h>

typedef struct _bytes 
{
	unsigned char* buffer;
	rsize_t buffer_len;
} *bytes;

#define TYPE_CAST(buf, type) ((type)(((bytes)(buf))->buffer))

void Append(bytes first, const bytes second, rsize_t offset, rsize_t count);

void AppendAt(bytes first, rsize_t offset1, const bytes second, rsize_t offset2, rsize_t count);

void RightTrim(bytes buf, size_t count);

void Patch(bytes dest, rsize_t offset1, const bytes second, rsize_t offset2, rsize_t count);

void SetBytes(bytes dest, uint8_t val, rsize_t offset, rsize_t count);

bool Equals(const bytes first, rsize_t offset1, const bytes second, rsize_t offset2, rsize_t count);

bytes CreateBytes(rsize_t length);

bytes FromBuffer(const void* src, rsize_t length);

bytes CreateEmpty();

bytes CopyBuffer(const bytes buf);

bytes TakeBufferSlice(const bytes buf, rsize_t offset, rsize_t count);

bool Reserve(bytes buf, rsize_t count);

void DeleteBytes(bytes buf);

//Creates an ut_array that contains byte buffers
//The buffers are fully owned by the array, sho should not be freed
//individually
UT_array* ListOfBuffers();

#endif