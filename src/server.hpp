#pragma once
#include "incl.hpp"
#include <vector>

struct ServerData
{
	std::mutex rooms_mutex;
	std::vector<Room> rooms;
};

SOCKET SetupServerSocket(USHORT port);
void StartServer();
void HandleConnection(ServerData* server_data, SOCKET other_socket);