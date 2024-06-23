#include "client.hpp"

static SOCKET SetupSocket(const char* address, USHORT port)
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

void ClientImplementation(const char* address, USHORT port)
{
	SOCKET client_socket = SetupSocket(address, port);


	MessageType msg = MESSAGE_TYPE_ROOM_QUERY;
	send(client_socket, reinterpret_cast<char*>(&msg), sizeof(MessageType), 0);
	{
		u32 rooms_count;
		recv(client_socket, reinterpret_cast<char*>(&rooms_count), sizeof(u32), 0);

		std::cout << "Available rooms          Room name          Users Connected          Max users\n";
		for (u32 i = 0; i < rooms_count; i++) {
			Room current_room;
			recv(client_socket, reinterpret_cast<char*>(&current_room), sizeof(Room), 0);
			std::cout << "Room: #" << i << ":                " << current_room.name << "              " <<
				current_room.current_pads << "                        " << current_room.max_pads << "\n";
		}
	}
	
	u32 chosen_room, slot;
	std::cout << "Choose the room to connect to:" << std::endl;
	std::cin >> chosen_room;

	msg = MESSAGE_TYPE_ROOM_JOIN;
	send(client_socket, reinterpret_cast<char*>(&msg), sizeof(MessageType), 0);
	send(client_socket, reinterpret_cast<char*>(&chosen_room), sizeof(u32), 0);
	recv(client_socket, reinterpret_cast<char*>(&slot), sizeof(u32), 0);

	XINPUT_STATE prev_pad_state = {};

	while (true) {

		XINPUT_STATE pad_state = {};

		s32 pad_read_result = XInputGetState(0, &pad_state);
		if (pad_read_result != ERROR_SUCCESS) {
		}

		if (std::memcmp(&prev_pad_state.Gamepad, &pad_state.Gamepad, sizeof(XINPUT_GAMEPAD)) != 0) {

			msg = MESSAGE_TYPE_SEND_PAD_DATA;
			send(client_socket, reinterpret_cast<char*>(&msg), sizeof(MessageType), 0);
			send(client_socket, reinterpret_cast<char*>(&chosen_room), sizeof(u32), 0);
			send(client_socket, reinterpret_cast<char*>(&slot), sizeof(u32), 0);

			s32 send_result = send(client_socket, reinterpret_cast<char*>(&pad_state.Gamepad), sizeof(XINPUT_GAMEPAD), 0);
			if (send_result == SOCKET_ERROR) {
				std::cout << "Could not send data to the host: " << WSAGetLastError();
				close_host_socket(client_socket);
				return;
			}
			prev_pad_state = pad_state;
		}


		Sleep(30);
	}


	close_host_socket(client_socket);
}