#pragma once
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <Xinput.h>
#include <ViGEm/Client.h>

#include <ws2tcpip.h>
#include <winsock2.h>

#include <iostream>
#include <thread>

#include "types.hpp"

//TODO this needs to be moved elsewhere some time in the future
static void close_host_socket(SOCKET socket)
{
	closesocket(socket);
	WSACleanup();
}

enum ThreadType
{
	THREAD_TYPE_CLIENT,
	THREAD_TYPE_STOP,
};
