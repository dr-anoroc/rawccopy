#include <stdio.h>
#include <Windows.h>
#include "helpers.h"
#include "fileio.h"


struct _disk_reader {
	HANDLE fh;
	uint64_t offset;
	uint16_t sector_sz;
};


disk_reader OpenDiskReader(const string file_name, uint16_t sector_sz)
{
	HANDLE fh = CreateFileW(BaseString(file_name), GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fh == INVALID_HANDLE_VALUE)
	{
		wchar_t buffer[5000];
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), 0, buffer, 5000, NULL);
		wprintf(L"Create File Error: %ls\n", buffer);
		return NULL;
	}

	SafeCreate(result, disk_reader);
	result->fh = fh;
	result->offset = 0;
	result->sector_sz = sector_sz;

	return result;
}

void CloseDiskReader(disk_reader dr)
{
	free(dr);
}

bool AppendBytesFromDiskRdr(disk_reader dr, int64_t offset, uint64_t cnt, bytes dest, uint64_t pos)
{
	if (offset < 0)
		offset = dr->offset;

	//There are several alignment constraints to protect against.
	//First at the left side (where we start reading):
	// * this should be sector aligned (within the file)
	// * it can only be written to even memory locations
	//If either of these two is not met, we first read the whole thing (!) 
	//in a temporary buffer and copy that back in dest.
	//The copying is heavy a penalty for calling the function
	//with non aligned parameters

	uint64_t al_offset = offset - (offset % dr->sector_sz);
	//At the right side, ie where we stop reading, we must also be sector
	//aligned, ie we can only read complete sectors:
	uint64_t al_cnt = ((cnt + offset + dr->sector_sz - 1) / dr->sector_sz) * dr->sector_sz - al_offset;

	if (al_offset != offset || ((uint64_t)dest->buffer + pos) & 1ULL)
	{
		//In the recursion, this call will not come here, because it can create
		//it's own buffer, and reading is sector aligned aligned
		bytes al_buf = GetBytesFromDiskRdr(dr, al_offset, al_cnt);
		if (al_buf)
		{
			AppendAt(dest, (rsize_t)pos, al_buf, (rsize_t)(offset - al_offset), (rsize_t)cnt);
			DeleteBytes(al_buf);
			return true;
		}
		else
			return false;
	}


	if (offset != dr->offset)
	{
		uint32_t low = offset & 0x0000000000FFFFFFFF;
		uint32_t high = offset >> 32;
		SetFilePointer(dr->fh, low, &high, FILE_BEGIN);
	}

	if (!Reserve(dest, (rsize_t)pos + (rsize_t)al_cnt))
		return false;

	DWORD bytes_read = 0;
	if (ReadFile(dr->fh, dest->buffer + pos, (DWORD)al_cnt, &bytes_read, NULL) && bytes_read == al_cnt)
	{
		RightTrim(dest, dest->buffer_len - (rsize_t)(pos + cnt));
		dr->offset = offset + cnt;
		return true;
	}
	else
	{
		return false;
	}
}

bytes GetBytesFromDiskRdr(disk_reader dr, int64_t offset, uint64_t cnt)
{
	bytes result = CreateEmpty();
	if (!result)
		return NULL;

	if (!AppendBytesFromDiskRdr(dr, offset, cnt, result, 0))
		return ErrorCleanUp(DeleteBytes, result, "");

	return result;
}
