#include <winsock2.h>
#include <Ws2tcpip.h>


#include "helpers.h"
#include "network.h"
#include "safe-string.h"

bool ParseIPDestination(const char* destination, uint32_t *ip, uint16_t* port)
{
	char* addr_part = strrchr(destination, ':');
	if (!addr_part)
		return false;

	*port = (uint16_t)strtoul(addr_part + 1, NULL, 10);
	if (*port <= 0 || *port > 0xFFFF)
		return false;

	char* addr_buf = calloc(addr_part - destination + 1, 1);
	strncpy(addr_buf, destination, addr_part - destination);

	WORD version_req = MAKEWORD(2, 2);
	WSADATA wsa_data;

	if (WSAStartup(version_req, &wsa_data) != 0)
		return false;

	*ip = INADDR_NONE;
	*ip = inet_addr(addr_buf);

	bool result = *ip != INADDR_NONE && *ip != INADDR_ANY;

	if (!result)
	{
		ADDRINFOA hints;
		memset(&hints, 0, sizeof(ADDRINFOA));
		hints.ai_family = AF_INET;

		ADDRINFOA* name_results;
		if (GetAddrInfoA(addr_buf, NULL, &hints, &name_results) == 0)
		{
			ADDRINFOA* res;
			//Loop and test are not really necessary as we filtered on 'AF_INET' while
			//searching, but it's clearer and offers some safety
			for (res = name_results; res; res = res->ai_next)
			{
				if (res->ai_family == AF_INET)
				{
					#pragma warning( push )
					#pragma warning( disable : 4133 )
					struct in_addr* ad = res->ai_addr->sa_data + 2;
					#pragma warning( pop )
					*ip = ad->S_un.S_addr;
					result = true;
				}
			}
			FreeAddrInfoA(name_results);
		}
	}
	WSACleanup();
	free(addr_buf);
	return result;

}


SOCKET GetConnectedSocket(uint32_t ip, uint16_t port)
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

	client_svc.sin_family = AF_INET;
	client_svc.sin_addr.S_un.S_addr = ip;
	client_svc.sin_port = htons(port);

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