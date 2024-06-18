#pragma once
#include "incl.hpp"

struct ConnectionInfo
{
	SOCKET client_socket = INVALID_SOCKET;
	std::thread client_thread;
	std::atomic_bool thread_running = true;
	XINPUT_STATE new_pad_input;
	bool updated = true;
};


SOCKET setup_host_socket(USHORT port);
void host_implementation();
void handle_connection(ConnectionInfo& connection_info);
