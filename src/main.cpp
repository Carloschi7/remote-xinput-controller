//#define SERVER_IMPL
#if defined SERVER_IMPL

#include "server.hpp"

int main() 
{
	StartServer();
}

#else

#include "host.hpp"
#include "client.hpp"

int main()
{
	char controller_test;
	std::cout << "Welcome to:\n";
	std::cout << "X    X X X    X XXXXX  X   X XXXXX       XXXX X     X X   X\n";
	std::cout << " X  X  X XX   X X    X X   X   X         X    XX   XX X   X\n";
	std::cout << "  XX   X X X  X XXXXX  X   X   X         XXX  X X X X X   X\n";
	std::cout << "  XX   X X  X X X      X   X   X   XXXX  XXX  X  X  X X   X\n";
	std::cout << " X  X  X X   XX X      X   X   X         X    X     X X   X\n";
	std::cout << "X    X X X    X X       XXX    X         XXXX X     X  XXX \n";
	std::cout << "\n";
	std::cout << "Before the fun begins, you optionally can test if the program detects correctly your controller according to its type!\n";
	do {
		std::cout << "Choose an action\n";
		std::cout << "Find DualShock pads! (D), find Xbox pads (X), any other button to connect to the hosting server:\n";
		std::cin >> controller_test;

		if (controller_test == 'D') {
			u32 num = QueryDualshockControllers(nullptr);
			if (num == 0) {
				std::cout << "No DualShock controller detected, check if the pad is detected by the system\n";
			}
			else {
				std::cout << num << " Dualshock controller(s) detected!\n";
			}
		}

		if (controller_test == 'X') {
			u32 num = QueryXboxControllers(nullptr);
			if (num == 0) {
				std::cout << "No Xbox controller detected, check if the pad is detected by the system\n";
			}
			else {
				std::cout << num << " Xbox controller(s) detected!\n";
			}
		}
	} while (controller_test == 'D' || controller_test == 'X');

	char action;
	std::string ip;
	std::cout << "insert the server IP:\n";
	std::cin >> ip;

	SOCKET local_socket = ConnectToServer(ip.c_str(), 20000);
	if (local_socket == INVALID_SOCKET) {
		std::cout << "Could not connect to that server\n";
		return -1;
	}

	while (true) {
		QueryRooms(local_socket);
		std::cout << "Create a room (C), join a room (J), Query rooms again(Q) {Anything else to exit}\n";
		std::cin >> action;

		if (action == 'C')
			HostImplementation(local_socket);
		else if (action == 'J')
			ClientImplementation(local_socket);
		else if (action == 'Q') {} // Just repeate the loop
		else break;
	}

	closesocket(local_socket);
	WSACleanup();
}
#endif


