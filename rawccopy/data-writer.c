#include <winsock2.h>
#include <stdio.h>

#include "network.h"
#include "data-writer.h"
#include "helpers.h"

struct _data_writer
{
	HANDLE fh;
	SOCKET socket;
};

data_writer TCPWriter(uint32_t ip, uint16_t port)
{
	SafeCreate(result, data_writer);
	result->fh = INVALID_HANDLE_VALUE;

	if ((result->socket = GetConnectedSocket(ip, port)) == INVALID_SOCKET)
		return ErrorCleanUp(free, result, "");

	return result;
}

data_writer FileWriter(const string file_name)
{

	HANDLE fh = CreateFileW(BaseString(file_name), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fh == INVALID_HANDLE_VALUE)
	{
		wchar_t buffer[5000];
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), 0, buffer, 5000, NULL);
		wprintf(L"Create File Error: %ls\n", buffer);
		return NULL;
	}

	SafeCreate(result, data_writer);
	result->fh = fh;

	return result;
}


void CloseDataWriter(data_writer writer)
{
	if (writer->fh == INVALID_HANDLE_VALUE)
		CleanUpSocket(writer->socket);
	else
	{
		CloseHandle(writer->fh);
	}

	free(writer);
}

bool WriteData(const data_writer wr, const bytes data)
{
	if (wr->fh == INVALID_HANDLE_VALUE)
	{
		int result = SendData(wr->socket, data);
		if (result == SOCKET_ERROR)
			return CleanUpAndFail(NULL, NULL, "Error TCPSend: %d\n", WSAGetLastError());
		return true;
	}
	else
	{
		DWORD bytes_written = 0;
		return WriteFile(wr->fh, data->buffer, (DWORD)data->buffer_len, &bytes_written, NULL) &&
					bytes_written == data->buffer_len;
	}
}
