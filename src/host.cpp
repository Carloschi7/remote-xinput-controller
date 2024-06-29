#include "host.hpp"

void SimulateDualshock()
{
	PVIGEM_CLIENT client = vigem_alloc();
	const auto connection = vigem_connect(client);

	PVIGEM_TARGET pad_handle = vigem_target_ds4_alloc();
	const auto controller_connection = vigem_target_add(client, pad_handle);

	while (true) {
		DS4_REPORT report = {};

		memset(&report, 0, sizeof(report));
		report.wButtons = 0;
		report.bThumbLX = 128; // Neutral position
		report.bThumbLY = 128; // Neutral position
		report.bThumbRX = 128; // Neutral position
		report.bThumbRY = 128; // Neutral position
		report.bTriggerL = 0;
		report.bTriggerR = 0;

		DS4_SET_DPAD(&report, DS4_BUTTON_DPAD_NONE);
		report.wButtons |= DS4_BUTTON_CROSS;
		vigem_target_ds4_update(client, pad_handle, report);
		Sleep(100);

		report.wButtons = 0;
		DS4_SET_DPAD(&report, DS4_BUTTON_DPAD_NONE);
		vigem_target_ds4_update(client, pad_handle, report);
		Sleep(3000);
	}
}

SOCKET ConnectToServer(const char* address, USHORT port)
{
	SOCKET connecting_socket = INVALID_SOCKET;
	WSADATA wsaData;
	s32 result = 0;

	s32 wsa_startup = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (wsa_startup != 0) {
		std::cout << "WSAStartup failed: " << wsa_startup;
		return INVALID_SOCKET;
	}

	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(port);
	inet_pton(AF_INET, address, &serverAddress.sin_addr);

	connecting_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr* socket_info = (sockaddr*)&serverAddress;
	result = connect(connecting_socket, socket_info, sizeof(sockaddr));
	if (result == -1) {
		closesocket(connecting_socket);
		return INVALID_SOCKET;
	}

	return connecting_socket;
}

void VigemDeallocate(PVIGEM_CLIENT client, ConnectionInfo* client_connections, u32 count)
{
	for (u32 i = 0; i < count; i++) {
		vigem_target_remove(client, client_connections[i].pad_handle);
		vigem_target_free(client_connections[i].pad_handle);
	}
	delete[] client_connections;
	vigem_disconnect(client);
	vigem_free(client);
}



void HostImplementation(SOCKET host_socket)
{
	u32 physical_pads = 0, virtual_pads = 0;
	for (u32 i = 0; i < XUSER_MAX_COUNT; i++) {
		XINPUT_STATE state;
		if (XInputGetState(i, &state) == ERROR_SUCCESS)
			physical_pads++;
	}

	std::cout << "Numbers of xbox pads to connect (" << XUSER_MAX_COUNT - physical_pads << " slots available)\n";
	std::cin >> virtual_pads;

	if (virtual_pads > XUSER_MAX_COUNT - physical_pads) {
		std::cout << "Not enough space\n";
		return;
	}

	Room::Info room_info;
	room_info.current_pads = 0;
	room_info.max_pads = virtual_pads;
	{
		std::string room_name;
		std::cout << "Insert room name:\n";
		std::cin >> room_name;
		if (room_name.size() >= sizeof(room_info.name)) {
			std::memcpy(room_info.name, "unknown", sizeof("unknown"));
		}
		else {
			std::memcpy(room_info.name, room_name.c_str(), room_name.size());
			room_info.name[room_name.size()] = 0;
		}
	}

	PVIGEM_CLIENT client = vigem_alloc();
	const auto connection = vigem_connect(client);

	if (!VIGEM_SUCCESS(connection))
	{
		std::cout << "To run the server u need to have vigem installed\n";
		return;
	}
	//TODO just make an array of PVIGEM_TARGETS
	ConnectionInfo* client_connections = new ConnectionInfo[virtual_pads];

	SendMsg(host_socket, MESSAGE_REQUEST_ROOM_CREATE);
	Send(host_socket, room_info);

	std::atomic<char> exit_val;
	std::thread host_input_thd([&exit_val]() {char ch; std::cin >> ch; exit_val.store(ch); });

	for (u32 i = 0; i < virtual_pads;) {

		//TODO insert a valid identifier to receive the user
		Message msg = ReceiveMsg(host_socket);

		if (msg == MESSAGE_INFO_SERVER_PING) {
			if (exit_val.load() == 'X') {
				std::cout << "Closing room...\n";
				SendMsg(host_socket, MESSAGE_INFO_ROOM_CLOSING);
				host_input_thd.join();
				VigemDeallocate(client, client_connections, i);
				return;
			}
		}
		else {

			ConnectionInfo& connection = client_connections[i];
			connection.pad_handle = vigem_target_x360_alloc();
			const auto controller_connection = vigem_target_add(client, connection.pad_handle);

			if (!VIGEM_SUCCESS(controller_connection)) {
				std::cout << "ViGEm Bus connection failed with error code: " << std::hex << controller_connection;
				SendMsg(host_socket, MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD);
			}
			else {
				SendMsg(host_socket, MESSAGE_INFO_PAD_ALLOCATED);
			}

			std::cout << "Connection found!!" << std::endl;
			i++;
		}
	}


	while (true) {

		Message msg = ReceiveMsg(host_socket);
		switch (msg) {
		case MESSAGE_INFO_SERVER_PING: {
			//Closing the room
			if (exit_val.load() == 'X') {
				std::cout << "Closing room...\n";
				SendMsg(host_socket, MESSAGE_INFO_ROOM_CLOSING);
				host_input_thd.join();
				VigemDeallocate(client, client_connections, virtual_pads);
				return;
			}
		}break;
		case MESSAGE_REQUEST_SEND_PAD_DATA: {
			PadSignal signal;
			u32 error_code = Receive(host_socket, &signal);

			vigem_target_x360_update(client, client_connections[signal.pad_number].pad_handle, *reinterpret_cast<XUSB_REPORT*>(&signal.pad_state));
			std::cout << "Signal from pad " << signal.pad_number << "\n";
			std::cout << signal.pad_state.wButtons << std::endl;
		}break;
		}
	}
}
