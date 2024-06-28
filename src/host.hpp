#pragma once
#include "incl.hpp"
#include <condition_variable>

struct ConnectionInfo
{
	PVIGEM_TARGET pad_handle;
	XINPUT_STATE pad_input;
};

SOCKET ConnectToServer(const char* address, USHORT port);
void HostImplementation(SOCKET host_socket);
