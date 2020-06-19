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

typedef struct _bytes* bytes;

unsigned char Byte(const bytes from, rsize_t index);

void Append(bytes first, const bytes second, rsize_t offset, rsize_t count);

void AppendAt(bytes first, rsize_t offset1, const bytes second, rsize_t offset2, rsize_t count);

void RightTrim(bytes buf, size_t count);

void Patch(bytes dest, rsize_t offset1, const bytes second, rsize_t offset2, rsize_t count);

uint64_t ReadNumber(const bytes from, rsize_t offset, rsize_t count);

int64_t ReadSignedNumber(const bytes from, rsize_t offset, rsize_t count);

bool Equals(const bytes first, rsize_t offset1, const bytes second, rsize_t offset2, rsize_t count);

bool EqualsBuffer(const bytes first, rsize_t offset, const unsigned char* second, rsize_t count);

bool Same(const bytes buf, rsize_t offset, unsigned char value, rsize_t count);

wchar_t* ToString(const bytes buf);

void WriteToFile(FILE *f, const bytes buf);

bytes CreateBytes(rsize_t length);

bytes FromBuffer(const void* src, rsize_t length);

bytes CreateEmpty();

bytes CopyBuffer(const bytes buf);

bytes TakeBufferSlice(const bytes buf, rsize_t offset, rsize_t count);

bool Reserve(bytes buf, rsize_t count);

void DeleteBytes(bytes buf);

UT_array* CreateBufferList();

void AppendBuffer(UT_array* bufferList, const bytes src, rsize_t offset, rsize_t count);

#endif