#ifndef NETWORK_H
#define NETWORK_H

//#include <winsock2.h>
#include <stdbool.h>
#include "byte-buffer.h"

//typedef uint64_t TCP_SOCKET;

bool ParseIPDestination(const char* address, wchar_t* ip, wchar_t* port);

SOCKET GetConnectedSocket(const wchar_t* ip, const wchar_t* port);

void CleanUpSocket(SOCKET socket);

bool SendData(SOCKET socket, const bytes data);


#endif //! NETWORK_H