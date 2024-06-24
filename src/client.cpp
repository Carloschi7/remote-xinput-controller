#include "client.hpp"

void QueryRooms(SOCKET client_socket)
{
	Message msg = MESSAGE_REQUEST_ROOM_QUERY;
	send(client_socket, reinterpret_cast<char*>(&msg), sizeof(Message), 0);

	u32 rooms_count;
	recv(client_socket, reinterpret_cast<char*>(&rooms_count), sizeof(u32), 0);

	std::cout << "Available rooms          Room name          Users Connected          Max users\n";
	for (u32 i = 0; i < rooms_count; i++) {
		Room::Info room_info;
		recv(client_socket, reinterpret_cast<char*>(&room_info), sizeof(Room::Info), 0);

		std::string name_padding = "                   ";
		for (u32 i = 0; i < sizeof(room_info.name) && room_info.name[i] != 0; i++) {
			name_padding.pop_back();
		}

		std::cout << "Room: #" << i << ":                " << room_info.name << name_padding.c_str() <<
			room_info.current_pads << "                        " << room_info.max_pads << "\n";
	}
}


void ClientImplementation(SOCKET client_socket)
{
	Message connection_status;
	u32 chosen_room;

	do {
		std::cout << "Choose the room to connect to:" << std::endl;
		std::cin >> chosen_room;

		Message msg = MESSAGE_REQUEST_ROOM_JOIN;

		send(client_socket, reinterpret_cast<char*>(&msg), sizeof(Message), 0);
		send(client_socket, reinterpret_cast<char*>(&chosen_room), sizeof(u32), 0);
		recv(client_socket, reinterpret_cast<char*>(&connection_status), sizeof(Message), 0);

		if (connection_status == MESSAGE_ERROR_ROOM_AT_FULL_CAPACITY) {
			std::cout << "Could not connect, the room is currently at full capacity\n";
		}
		else if (connection_status == MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD) {
			std::cout << "The host had issues creating a virtual pad, please try later\n";
		}

	} while (connection_status != MESSAGE_ERROR_NONE);

	std::cout << "Connection was successful!\n";

	XINPUT_STATE prev_pad_state = {};
	while (true) {

		XINPUT_STATE pad_state = {};

		s32 pad_read_result = XInputGetState(0, &pad_state);
		if (pad_read_result != ERROR_SUCCESS) {
		}

		if (std::memcmp(&prev_pad_state.Gamepad, &pad_state.Gamepad, sizeof(XINPUT_GAMEPAD)) != 0) {

			Message msg = MESSAGE_REQUEST_SEND_PAD_DATA;
			send(client_socket, reinterpret_cast<char*>(&msg), sizeof(Message), 0);
			send(client_socket, reinterpret_cast<char*>(&chosen_room), sizeof(u32), 0);

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

