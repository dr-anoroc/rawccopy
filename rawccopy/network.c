#include <winsock2.h>
#include <Ws2tcpip.h>


#include "helpers.h"
#include "network.h"

bool ParseIPDestination(const char* address, wchar_t* ip, wchar_t* port)
{
	wchar_t* input_ad = ToWideString(address, 0);

	wchar_t* port_part = wcspbrk(input_ad, L":");
	if (port_part++ == 0 || !IsDigitsW(port_part) || wcstoul(port_part, NULL, 10) > 65535)
	{
		free(input_ad);
		return false;
	}

	wcscpy_s(port, 20, port_part);

	rsize_t addr_len = port_part - input_ad - 1;

	input_ad[addr_len] = 0;
	int test_len = -1;
	if (swscanf_s(input_ad, L"%*3l[0-9].%*3l[0-9].%*3l[0-9].%*3l[0-9]%n", &test_len) == 0 && test_len == addr_len)
	{
		wcscpy_s(ip, 16, input_ad);
		free(input_ad);
		return true;
	}

	WORD version_req = MAKEWORD(2, 2);
	WSADATA wsa_data;

	if (WSAStartup(version_req, &wsa_data) != 0)
	{
		free(input_ad);
		return false;
	}

	ADDRINFOW hints;
	memset(&hints, 0, sizeof(ADDRINFOW));
	hints.ai_family = AF_INET;

	ADDRINFOW* name_results;
	if (GetAddrInfoW(input_ad, NULL, &hints, &name_results) != 0)
	{
		free(input_ad);
		return false;
	}

	ADDRINFOW* res;
	//Loop and test are not really necessary as we filtered on 'AF_INET' while
	//searching, but it's clearer and offers some safety
	bool success = false;
	for (res = name_results; res; res = res->ai_next)
	{
		if (res->ai_family == AF_INET)
		{
			DWORD ipAddrLen = MAX_LEN;
			if (WSAAddressToStringW((LPSOCKADDR)res->ai_addr, (DWORD)res->ai_addrlen, NULL, (wchar_t*)ip,
				&ipAddrLen) == 0)
			{
				success = true;
				break;
			}
		}
	}

	FreeAddrInfoW(name_results);

	WSACleanup();
	free(input_ad);
	return success;
}


SOCKET GetConnectedSocket(const wchar_t* ip, const wchar_t* port)
{
	WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsa_data;

	if (WSAStartup(wVersionRequested, &wsa_data) != 0)
		return INVALID_SOCKET;

	SOCKET result = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (result == INVALID_SOCKET)
	{
		WSACleanup();
		return INVALID_SOCKET;
	}

	struct sockaddr_in client_svc;
	int ips[4];
	swscanf_s(ip, L"%3d.%3d.%3d.%3d", ips, ips + 1, ips + 2, ips + 3);

	client_svc.sin_family = AF_INET;
	client_svc.sin_addr.S_un.S_addr = ((((((*(ips + 3) << 8) + *(ips + 2))) << 8) + *(ips + 1)) << 8) + *ips;
	client_svc.sin_port = htons((uint16_t)wcstoul(port, NULL, 10));

	if (connect(result, (SOCKADDR*)&client_svc, sizeof(client_svc)) == SOCKET_ERROR)
	{
		wprintf(L"TCP Connect error: %d\n", WSAGetLastError());
		closesocket(result);
		return INVALID_SOCKET;
	}
	return result;
}

bool SendData(SOCKET socket, const bytes data)
{
	return (send(socket, data->buffer, (int)data->buffer_len, 0));
}

void CleanUpSocket(SOCKET socket)
{
	shutdown(socket, SD_BOTH);
	closesocket(socket);
	WSACleanup();
}