#include <winsock2.h>
#include <stdio.h>

#include "network.h"
#include "data-writer.h"
#include "helpers.h"

struct _data_writer
{
	wchar_t file_name[MAX_NTFS_PATH];
	HANDLE fh;
	SOCKET socket;
};

data_writer TCPWriter(const wchar_t* ip, const wchar_t* port)
{
	SafeCreate(result, data_writer);
	memset(result, 0, sizeof(*result));

	if ((result->socket = GetConnectedSocket(ip, port)) == INVALID_SOCKET)
		return ErrorCleanUp(free, result, "");

	return result;
}

data_writer FileWriter(const wchar_t* file_name)
{

	HANDLE fh = CreateFileW(file_name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fh == INVALID_HANDLE_VALUE)
	{
		wchar_t buffer[5000];
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), 0, buffer, 5000, NULL);
		wprintf(L"Create File Error: %ls\n", buffer);
		return NULL;
	}

	SafeCreate(result, data_writer);
	memset(result, 0, sizeof(*result));
	result->fh = fh;
	wcscpy_s(result->file_name, MAX_NTFS_PATH, file_name);

	return result;
}

wchar_t* CurrentFileName(const data_writer writer)
{
	return writer->file_name;
}

void CloseDataWriter(data_writer writer)
{
	if (!*(writer->file_name))
		CleanUpSocket(writer->socket);
	else
	{
		CloseHandle(writer->fh);
	}

	free(writer);
}

bool WriteData(const data_writer wr, const bytes data)
{
	if (!*(wr->file_name))
	{
		int result = SendData(wr->socket, data);
		if (result == SOCKET_ERROR)
		{
			printf("Error TCPSend: %d\n", WSAGetLastError());
			return false;
		}
		else
			return true;
	}
	else
	{
		DWORD bytes_written = 0;
		return WriteFile(wr->fh, data->buffer, (DWORD)data->buffer_len, &bytes_written, NULL) &&
					bytes_written == data->buffer_len;
	}
}
