#ifndef NETWORK_H
#define NETWORK_H


#include <stdbool.h>
#include "byte-buffer.h"



bool ParseIPDestination(const char* destination, uint32_t* ip, uint16_t* port);

SOCKET GetConnectedSocket(uint32_t ip, uint16_t port);

void CleanUpSocket(SOCKET socket);

bool SendData(SOCKET socket, const bytes data);


#endif //! NETWORK_H