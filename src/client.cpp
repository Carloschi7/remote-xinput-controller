#include "client.hpp"

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