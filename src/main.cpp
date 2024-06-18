#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <Xinput.h>
#include <ViGEm/Client.h>

#include <ws2tcpip.h>
#include <winsock2.h>

#include <iostream>
#include <thread>

void close_host_socket(SOCKET socket)
{
	closesocket(socket);
	WSACleanup();
}


//TODO use
struct ConnectionInfo
{
	SOCKET client_socket;
	std::thread client_thread;
	XINPUT_STATE new_pad_input;
	bool updated = true;
};

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

void client_implementation(const char* address, USHORT port)
{
	SOCKET client_socket = INVALID_SOCKET;
	WSADATA wsaData;
	int result = 0;

	int wsa_startup = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (wsa_startup != 0) {
		std::cout << "WSAStartup failed: " << wsa_startup;
		return;
	}

	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(port);
	inet_pton(AF_INET, address, &serverAddress.sin_addr);

	client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr* socket_info = (sockaddr*)&serverAddress;
	result = connect(client_socket, socket_info, sizeof(sockaddr));
	if (result == -1) {
		close_host_socket(client_socket);
		return;
	}

	while (true) {

		XINPUT_STATE pad_state = {};

		int pad_read_result = XInputGetState(0, &pad_state);
		if (pad_read_result == ERROR_SUCCESS) {
			int send_result = send(client_socket, reinterpret_cast<char*>(&pad_state), sizeof(XINPUT_STATE), 0);
			if (send_result == SOCKET_ERROR) {
				std::cout << "Could not send data to the host: " << WSAGetLastError();
				close_host_socket(client_socket);
				return;
			}

		}
		else {

		}
		Sleep(30);
	}


	close_host_socket(client_socket);
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

	SOCKET host_socket = setup_host_socket(20000);
	SOCKET client_socket = INVALID_SOCKET;
	client_socket = accept(host_socket, NULL, NULL);
	std::cout << "Connection found!!" << std::endl;



	while (true) {
		XINPUT_STATE pad_state = {};
		//TODO: move the waiting for new controller data on a separate thread
		int bytes_read = recv(client_socket, reinterpret_cast<char*>(&pad_state), sizeof(XINPUT_STATE), 0);
		if (WSAGetLastError() == WSAECONNRESET || bytes_read == 0) {
			std::cout << "Error while receiving bytes from client socket: Client disconnected\n";
			closesocket(host_socket);
			WSACleanup();
			return;
		}

		vigem_target_x360_update(client, controller, *reinterpret_cast<XUSB_REPORT*>(&pad_state.Gamepad));
	}

	vigem_target_remove(client, controller);
	vigem_target_free(controller);
	close_host_socket(client_socket);
}

int main() 
{
	std::string ans;
	std::cout << "Start server? (Y/N)\n";
	std::cin >> ans;
	if (ans == "Y")
		host_implementation();
	else {
		std::string ip;
		std::cout << "select an ip to connect to:\n";
		std::cin >> ip;
		client_implementation(ip.c_str(), 20000);
	}
}