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
	SOCKET client_socket = INVALID_SOCKET;
	std::thread client_thread;
	std::atomic_bool thread_running = true;
	XINPUT_STATE pad_input;
	bool updated = true;
};


SOCKET SetupHostSocket(USHORT port);
void HostImplementation();
void HandleConnection(HostConcurrencyData& hcd, ConnectionInfo& connection_info);
