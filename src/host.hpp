#pragma once
#include "incl.hpp"
#include <condition_variable>

struct ConnectionInfo
{
	PVIGEM_TARGET pad_handle;
	bool connected = false;
};

void TestXboxPad();
void TestDualshock();

void CaptureWindow(const char* process_name);
SOCKET ConnectToServer(const char* address, USHORT port);
void VigemDeallocate(PVIGEM_CLIENT client, ConnectionInfo* client_connections, u32 count);
void HostImplementation(SOCKET host_socket);
