#define SERVER_IMPL

#ifdef SERVER_IMPL

#include "server.hpp"

int main() 
{
	ServerImplementation();
}

#else

#include "host.hpp"
#include "client.hpp"



int main()
{
	std::string create_room_or_not, ip;

	std::cout << "insert the server IP:\n";
	std::cin >> ip;

	SOCKET local_socket = ConnectToServer(ip.c_str(), 20000);
	if (local_socket == INVALID_SOCKET) {
		std::cout << "Could not connect to that server\n";
		return -1;
	}

	QueryRooms(local_socket);

	std::cout << "Create a room? (Y/N)\n";
	std::cin >> create_room_or_not;

	if (create_room_or_not == "Y")
		HostImplementation(local_socket);
	else 
		ClientImplementation(local_socket);

	closesocket(local_socket);
	WSACleanup();
}
#endif

