#include "host.hpp"

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
	ConnectionInfo* client_connections = new ConnectionInfo[virtual_pads];

	SendMsg(host_socket, MESSAGE_REQUEST_ROOM_CREATE);
	Send(host_socket, room_info);

	for (u32 i = 0; i < virtual_pads; i++) {

		//TODO insert a valid identifier to receive the user
		Message msg = ReceiveMsg(host_socket);
		if (msg != MESSAGE_INFO_CLIENT_JOINING_ROOM) {
			//TODO prob should create an assert
		}

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
	}

	while (true) {
		PadSignal signal;
		u32 error_code = Receive(host_socket, &signal);
		
		vigem_target_x360_update(client, client_connections[signal.pad_number].pad_handle, *reinterpret_cast<XUSB_REPORT*>(&signal.pad_state));
		std::cout << "Signal from pad " << signal.pad_number << "\n";
		std::cout << signal.pad_state.wButtons << std::endl;
	}
}
