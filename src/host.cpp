#include "host.hpp"

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

SOCKET SetupHostSocket(USHORT port)
{
	SOCKET host_socket = INVALID_SOCKET;
	WSADATA wsaData;
	s32 result = 0;

	s32 wsa_startup = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (wsa_startup != 0) {
		std::cout << "WSAStartup failed: " << wsa_startup;
		return INVALID_SOCKET;
	}

	sockaddr_in server_address = {};
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = INADDR_ANY;
	server_address.sin_port = htons(port);

	host_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	result = bind(host_socket, (sockaddr*)&server_address, sizeof(server_address));
	if (result == SOCKET_ERROR) {
		//TODO Handle this
		closesocket(host_socket);
		WSACleanup();
		return INVALID_SOCKET;
	}

	if (listen(host_socket, SOMAXCONN) == SOCKET_ERROR) {
		std::cout << "listen failed with error: " << WSAGetLastError() << '\n';
		closesocket(host_socket);
		WSACleanup();
		return INVALID_SOCKET;
	}

	return host_socket;
}

void HostImplementation()
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

	PVIGEM_CLIENT client = vigem_alloc();
	const auto connection = vigem_connect(client);

	if (!VIGEM_SUCCESS(connection))
	{
		std::cout << "To run the server u need to have vigem installed\n";
		return;
	}

	HostConcurrencyData concurrency_data;
	ConnectionInfo* client_connections = new ConnectionInfo[virtual_pads];

	for (u32 i = 0; i < virtual_pads; i++) {
		ConnectionInfo& connection = client_connections[i];
		connection.pad_handle = vigem_target_x360_alloc();
		const auto controller_connection = vigem_target_add(client, connection.pad_handle);

		if (!VIGEM_SUCCESS(controller_connection))
		{
			std::cout << "ViGEm Bus connection failed with error code: " << std::hex << controller_connection;
			return;
		}


		SOCKET host_socket = SetupHostSocket(20000);
		connection.client_socket = accept(host_socket, NULL, NULL);
		connection.client_thread = std::thread([&concurrency_data, &connection]() { HandleConnection(concurrency_data, connection); });
		std::cout << "Connection found!!" << std::endl;
	}

	std::thread stop_thread([&concurrency_data]() { StopRoutine(concurrency_data); });
	std::unique_lock lk{ concurrency_data.notification_mutex };

	while (true) {
		concurrency_data.cond_var.wait(lk);

		//Means an ESC command was issued, so exit the loop
		if (concurrency_data.last_thread_to_notify == THREAD_TYPE_STOP)
			break;
		
		for (u32 i = 0; i < virtual_pads; i++) {
			ConnectionInfo& connection = client_connections[i];
			if (connection.updated) {
				vigem_target_x360_update(client, connection.pad_handle, *reinterpret_cast<XUSB_REPORT*>(&connection.pad_input.Gamepad));
				std::cout << connection.pad_input.Gamepad.wButtons << std::endl;
				connection.updated = false;
			}
		}
	}
	lk.unlock();

	for (u32 i = 0; i < virtual_pads; i++) {
		ConnectionInfo& connection = client_connections[i];
		connection.thread_running = false;
		closesocket(connection.client_socket);
		connection.client_thread.join();

		vigem_target_remove(client, connection.pad_handle);
		vigem_target_free(connection.pad_handle);
	}

	stop_thread.join();
}

void HandleConnection(HostConcurrencyData& hcd, ConnectionInfo& connection_info)
{
	XINPUT_STATE prev_pad_state = {};
	while (connection_info.thread_running) {
		//TODO: move the waiting for new controller data on a separate thread
		XINPUT_STATE pad_state = {};
		s32 bytes_read = recv(connection_info.client_socket, reinterpret_cast<char*>(&pad_state), sizeof(XINPUT_STATE), 0);
		if (WSAGetLastError() == WSAECONNRESET || bytes_read == 0) {
			std::cout << "Error while receiving bytes from client socket: Client disconnected\n";
			closesocket(connection_info.client_socket);
			connection_info.client_socket = INVALID_SOCKET;
			return;
		}

		if(std::memcmp(&prev_pad_state.Gamepad, &pad_state.Gamepad, sizeof(XINPUT_STATE)) != 0) {
			std::scoped_lock notification_lock{ hcd.notification_mutex };
			connection_info.pad_input = pad_state;
			connection_info.updated = true;
			hcd.last_thread_to_notify = THREAD_TYPE_CLIENT;
			hcd.cond_var.notify_all();
			prev_pad_state = pad_state;
		}
	}
}
