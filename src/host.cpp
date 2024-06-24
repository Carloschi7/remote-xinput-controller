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
		close_host_socket(connecting_socket);
		return INVALID_SOCKET;
	}

	return connecting_socket;
}

static void StopRoutine(HostConcurrencyData& hcd)
{
	while (true) {
		Sleep(50);
		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
			std::scoped_lock notification_lock{ hcd.notification_mutex };
			hcd.last_thread_to_notify = THREAD_TYPE_STOP;
			hcd.cond_var.notify_all();
			break;
		}
	}
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

	Room room;
	room.current_pads = 0;
	room.max_pads = virtual_pads;
	{
		std::string room_name;
		std::cout << "Insert room name:\n";
		std::cin >> room_name;
		if (room_name.size() >= sizeof(room.name)) {
			std::memcpy(room.name, "unknown", sizeof("unknown"));
		}
		else {
			std::memcpy(room.name, room_name.c_str(), room_name.size());
			room.name[room_name.size()] = 0;
		}
	}

	PVIGEM_CLIENT client = vigem_alloc();
	const auto connection = vigem_connect(client);

	if (!VIGEM_SUCCESS(connection))
	{
		std::cout << "To run the server u need to have vigem installed\n";
		return;
	}

	HostConcurrencyData concurrency_data;
	ConnectionInfo* client_connections = new ConnectionInfo[virtual_pads];

	Message msg = MESSAGE_REQUEST_ROOM_CREATE;
	send(host_socket, reinterpret_cast<char*>(&msg), sizeof(Message), 0);
	send(host_socket, reinterpret_cast<char*>(&room), sizeof(Room), 0);

	for (u32 i = 0; i < virtual_pads; i++) {

		//TODO insert a valid identifier to receive the user
		char new_user_name[16];
		u32 error_code = recv(host_socket, new_user_name, sizeof(new_user_name), 0);

		ConnectionInfo& connection = client_connections[i];
		connection.pad_handle = vigem_target_x360_alloc();
		const auto controller_connection = vigem_target_add(client, connection.pad_handle);

		if (!VIGEM_SUCCESS(controller_connection))
		{
			//TODO tell the server if vigem works fine
			std::cout << "ViGEm Bus connection failed with error code: " << std::hex << controller_connection;
			return;
		}

		//connection.client_thread = std::thread([&concurrency_data, &connection]() { HandleConnection(concurrency_data, connection); });
		std::cout << "Connection found!!" << std::endl;
	}

	while (true) {
		PadSignal signal;
		u32 error_code = recv(host_socket, reinterpret_cast<char*>(&signal), sizeof(PadSignal), 0);

		//Means an ESC command was issued, so exit the loop
		if (concurrency_data.last_thread_to_notify == THREAD_TYPE_STOP)
			break;
		
		vigem_target_x360_update(client, client_connections[signal.pad_number].pad_handle, *reinterpret_cast<XUSB_REPORT*>(&signal.pad_state));
		std::cout << "Signal from pad " << signal.pad_number << "\n";
		std::cout << signal.pad_state.wButtons << std::endl;
	}
}
