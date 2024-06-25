#include "client.hpp"

void QueryRooms(SOCKET client_socket)
{
	Message msg = MESSAGE_REQUEST_ROOM_QUERY;
	Send(client_socket, msg);

	u32 rooms_count;
	Receive(client_socket, &rooms_count);

	std::cout << "Available rooms          Room name          Users Connected          Max users\n";
	for (u32 i = 0; i < rooms_count; i++) {
		Room::Info room_info;
		Receive(client_socket, &room_info);

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

		Send(client_socket, msg);
		Send(client_socket, chosen_room);
		Receive(client_socket, &connection_status);

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
			Send(client_socket, msg);
			Send(client_socket, chosen_room);
			Send(client_socket, pad_state.Gamepad);

			prev_pad_state = pad_state;
		}


		Sleep(30);
	}
}

