#include "host.hpp"
#include <condition_variable>

std::condition_variable cond_var;
std::mutex notification_mutex;
std::thread stop_thread;

static void stop_routine(std::condition_variable& cv) {
	while (true) {
		Sleep(50);
		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
			std::scoped_lock notification_lock{ notification_mutex };
			cv.notify_all();
			break;
		}
	}
}

SOCKET setup_host_socket(USHORT port)
{
	SOCKET host_socket = INVALID_SOCKET;
	WSADATA wsaData;
	int result = 0;

	int wsa_startup = WSAStartup(MAKEWORD(2, 2), &wsaData);
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

void host_implementation()
{
	PVIGEM_CLIENT client = vigem_alloc();
	const auto connection = vigem_connect(client);

	if (!VIGEM_SUCCESS(connection))
	{
		std::cout << "To run the server u need to have vigem installed\n";
		return;
	}

	const auto controller = vigem_target_x360_alloc();
	const auto controller_connection = vigem_target_add(client, controller);

	if (!VIGEM_SUCCESS(controller_connection))
	{
		std::cout << "ViGEm Bus connection failed with error code: " << std::hex << controller_connection;
		return;
	}

	ConnectionInfo connection_info = {};
	SOCKET host_socket = setup_host_socket(20000);
	connection_info.client_socket = accept(host_socket, NULL, NULL);
	connection_info.client_thread = std::thread([&]() { Sleep(2000); handle_connection(connection_info); });
	std::cout << "Connection found!!" << std::endl;

	stop_thread = std::thread([]() { stop_routine(cond_var); });

	std::unique_lock lk{ notification_mutex };
	while (true) {
		cond_var.wait(lk);

		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
			break;
		
		if (connection_info.updated) {
			vigem_target_x360_update(client, controller, *reinterpret_cast<XUSB_REPORT*>(&connection_info.new_pad_input.Gamepad));
			std::cout << connection_info.new_pad_input.Gamepad.wButtons << std::endl;
			connection_info.updated = false;
		}
	}
	lk.unlock();

	connection_info.thread_running = false;
	closesocket(connection_info.client_socket);
	connection_info.client_thread.join();

	stop_thread.join();

	vigem_target_remove(client, controller);
	vigem_target_free(controller);
}

void handle_connection(ConnectionInfo& connection_info)
{
	XINPUT_STATE prev_pad_state = {};
	while (connection_info.thread_running) {
		//TODO: move the waiting for new controller data on a separate thread
		XINPUT_STATE pad_state = {};
		int bytes_read = recv(connection_info.client_socket, reinterpret_cast<char*>(&pad_state), sizeof(XINPUT_STATE), 0);
		if (WSAGetLastError() == WSAECONNRESET || bytes_read == 0) {
			std::cout << "Error while receiving bytes from client socket: Client disconnected\n";
			closesocket(connection_info.client_socket);
			connection_info.client_socket = INVALID_SOCKET;
			return;
		}

		if(std::memcmp(&prev_pad_state.Gamepad, &pad_state.Gamepad, sizeof(XINPUT_STATE)) != 0) {
			std::scoped_lock notification_lock{ notification_mutex };
			connection_info.new_pad_input = pad_state;
			connection_info.updated = true; 
			cond_var.notify_all();
			prev_pad_state = pad_state;
		}

	}
}
