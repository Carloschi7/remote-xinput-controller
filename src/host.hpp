#pragma once
#include "incl.hpp"
#include <condition_variable>

//Helps with thread synchronization with the threads which listen asynchronously
//for pad input
struct HostConcurrencyData
{
	std::condition_variable cond_var;
	std::mutex notification_mutex;
	ThreadType last_thread_to_notify = THREAD_TYPE_CLIENT;
};

struct ConnectionInfo
{
	PVIGEM_TARGET pad_handle;
	XINPUT_STATE pad_input;
};

SOCKET ConnectToServer(const char* address, USHORT port);
void HostImplementation(SOCKET host_socket);
