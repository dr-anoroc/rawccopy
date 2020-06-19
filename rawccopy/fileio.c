#include <stdio.h>
#include <Windows.h>
#include "helpers.h"
#include "fileio.h"


struct _file_reader
{
	HANDLE fh;
};

struct _cluster_reader
{
	HANDLE fh;
	uint64_t image_offs;
	rsize_t cluster_sz;
};

void SetFileOffset(HANDLE fh, uint64_t offset);
bytes BytesFromFile(HANDLE fh, uint64_t count);

cluster_reader OpenClusterReader(const wchar_t* file_name, uint64_t image_offs, rsize_t cluster_sz)
{
	HANDLE fh = CreateFileW(file_name, GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fh == INVALID_HANDLE_VALUE)
	{
		wchar_t buffer[5000];
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), 0, buffer, 5000, NULL);
		wprintf(L"Create File Error: %ls\n", buffer);
		return NULL;
	}

	SafeCreate(result, cluster_reader);
	result->fh = fh;
	result->cluster_sz = cluster_sz;
	result->image_offs = image_offs;
	return result;
}

void CloseClusterReader(cluster_reader cr)
{
	CloseHandle(cr->fh);
	free(cr);
}

rsize_t ClusterSize(const cluster_reader cr)
{
	return cr->cluster_sz;
}

bytes ReadClusters(const cluster_reader cr, uint32_t cluster_offs, uint32_t cluster_cnt)
{
	SetNextCluster(cr, cluster_offs);

	return ReadNextClusters(cr, cluster_cnt);
}

bytes ReadNextClusters(const cluster_reader cr, uint32_t cluster_cnt)
{
	bytes result = CreateBytes(cluster_cnt * cr->cluster_sz);
	if (!result)
		return NULL;

	if (!ReadNextClustersIn(cr, cluster_cnt, result))
		return ErrorCleanUp(DeleteBytes, result, "");

	return result;
}

bool ReadNextClustersIn(const cluster_reader cr, uint32_t cluster_cnt, bytes dst)
{
	Reserve(dst, cluster_cnt * cr->cluster_sz);
	RightTrim(dst, dst->buffer_len - cluster_cnt * cr->cluster_sz);
	DWORD byte_cnt = 0;
	return ReadFile(cr->fh, dst->buffer, (DWORD)dst->buffer_len, &byte_cnt, NULL) && byte_cnt == dst->buffer_len;
}

bool ReadClustersIn(const cluster_reader cr, uint64_t cluster_offs, uint32_t cluster_cnt, bytes dst)
{
	SetNextCluster(cr, cluster_offs);
	return ReadNextClustersIn(cr, cluster_cnt, dst);
}

void SetNextCluster(const cluster_reader cr, uint64_t cluster_offs)
{
	SetFileOffset(cr->fh, cr->image_offs + cluster_offs * cr->cluster_sz);
}

bytes ReadImageBytes(const cluster_reader cr, uint64_t byte_offs, uint64_t byte_cnt)
{

	SetFileOffset(cr->fh, cr->image_offs + byte_offs);

	return BytesFromFile(cr->fh, byte_cnt);
}


file_reader OpenFileReader(const wchar_t* file_name)
{
	HANDLE fh = CreateFileW(file_name, GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
										NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fh == INVALID_HANDLE_VALUE)
	{
		wchar_t buffer[5000];
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), 0, buffer, 5000, NULL);
		wprintf(L"Create File Error: %ls\n", buffer);
		return NULL;
	}

	SafeCreate(result, file_reader);
	result->fh = fh;
	return result;
}


void CloseFileReader(file_reader fr)
{
	CloseHandle(fr->fh);
	free(fr);
}


void SetOffset(const file_reader fr, uint64_t offset)
{
	SetFileOffset(fr->fh, offset);
}

bytes ReadNextBytes(const file_reader fr, uint64_t byte_cnt)
{
	return BytesFromFile(fr->fh, byte_cnt);
}

bytes ReadBytes(const file_reader fr, uint64_t offset, uint64_t byte_cnt)
{
	SetOffset(fr, offset);
	return ReadNextBytes(fr, byte_cnt);
}

bytes ReadBytesFromFile(const wchar_t* file_name, uint64_t offset, uint64_t byte_cnt)
{
	file_reader fr = OpenFileReader(file_name);
	if (!fr)
		return NULL;
	SetOffset(fr, offset);
	bytes result = ReadNextBytes(fr, byte_cnt);
	CloseFileReader(fr);
	return result;
}

bool ReadNextBytesIn(bytes dest, const file_reader fr, uint64_t byte_cnt)
{
	if (dest->buffer_len < byte_cnt)
		ErrorExit("Not enough space in byte buffer", -1);

	DWORD bytes_read = 0;
	return (ReadFile(fr->fh, dest->buffer, (DWORD)byte_cnt, &bytes_read, NULL) && bytes_read == byte_cnt);
}

bool ReadBytesIn(bytes dest, const file_reader fr, uint64_t offset, uint64_t byte_cnt)
{
	SetOffset(fr, offset);
	return ReadNextBytesIn(dest, fr, byte_cnt);
}

void SetFileOffset(HANDLE fh, uint64_t offset)
{
	uint32_t low = offset & 0x0000000000FFFFFFFF;
	uint32_t high = offset >> 32;
	SetFilePointer(fh, low, &high, FILE_BEGIN);
}

bytes BytesFromFile(HANDLE fh, uint64_t num_bytes)
{
	bytes result = CreateEmpty();
	if (!result)
		return NULL;

	while (num_bytes > 0)
	{
		uint32_t to_read = (uint32_t)min(num_bytes, MAXUINT32);
		if (!Reserve(result, result->buffer_len + to_read))
			return ErrorCleanUp(DeleteBytes, result, "");

		DWORD byte_cnt = 0;
		if (!ReadFile(fh, result->buffer + result->buffer_len - to_read, (DWORD)to_read, &byte_cnt, NULL) ||
			byte_cnt != to_read)
			return ErrorCleanUp(DeleteBytes, result, "");

		num_bytes -= to_read;
	}
	return result;
}