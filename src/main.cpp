#include "incl.hpp"
#if defined SERVER_IMPL

#include "server.hpp"

int main() 
{
	StartServer();
}

#else

#include "host.hpp"
#include "client.hpp"

namespace Audio {
	u32 FillAudioBuffer();
	s32 CaptureSystemAudio();
	HRESULT RenderAudioFramev();
}


int main()
{
	//Audio::RenderAudioFramev();
	//Audio::FillAudioBuffer();
	//Audio::CaptureSystemAudio();

	char controller_test;
	Log::Format("Welcome to:\n");
	Log::Format("X    X X X    X XXXXX  X   X XXXXX       XXXX X     X X   X\n");
	Log::Format(" X  X  X XX   X X    X X   X   X         X    XX   XX X   X\n");
	Log::Format("  XX   X X X  X XXXXX  X   X   X         XXX  X X X X X   X\n");
	Log::Format("  XX   X X  X X X      X   X   X   XXXX  XXX  X  X  X X   X\n");
	Log::Format(" X  X  X X   XX X      X   X   X         X    X     X X   X\n");
	Log::Format("X    X X X    X X       XXX    X         XXXX X     X  XXX \n");
	Log::Format("\n");
	Log::Format("Before the fun begins, you optionally can test if the program detects correctly your controller according to its type!\n");
	do {
		Log::Format("Choose an action\n");
		Log::Format("Find DualShock pads! (D), find Xbox pads (X), any other button to connect to the hosting server:\n");
		std::cin >> controller_test;

		if (controller_test == 'D') {
			u32 num = QueryDualshockCount();
			if (num == 0) {
				Log::Format("No DualShock controller detected, check if the pad is detected by the system\n");
			}
			else {
				Log::Format("{} Dualshock controller(s) detected!\n", num);
			}
		}

		if (controller_test == 'X') {
			u32 num = QueryXboxCount();
			if (num == 0) {
				Log::Format("No Xbox controller detected, check if the pad is detected by the system\n");
			}
			else {
				Log::Format("{} Xbox controller(s) detected!\n", num);
			}
		}
	} while (controller_test == 'D' || controller_test == 'X');

	char action;
	std::string ip;
	Log::Format("insert the server IP:\n");
	std::cin >> ip;

	SOCKET local_socket = ConnectToServer(ip.c_str(), 20000);
	if (local_socket == INVALID_SOCKET) {
		Log::Format("Could not connect to that server\n");
		return -1;
	}

	while (true) {
		QueryRooms(local_socket);
		Log::Format("Create a room (C), join a room (J), Query rooms again(Q) (Anything else to exit)\n");
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


