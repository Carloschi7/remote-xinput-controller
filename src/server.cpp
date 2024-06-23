#include "server.hpp"
#include <vector>
#include <iostream>

struct HostMessage
{
	SOCKET host_socket;
	MessageType type;
};

std::mutex message_queue_mutex;
std::vector<std::thread> host_threads;
std::vector<Room> rooms;

SOCKET SetupServerSocket(USHORT port)
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

void ServerImplementation()
{
	SOCKET server_socket = SetupServerSocket(20000);

	//TODO add a rooms mutex
	auto handle_connection = [](SOCKET other_socket) {
		while (true) {
			MessageType msg;
			u32 error_msg = recv(other_socket, reinterpret_cast<char*>(&msg), sizeof(MessageType), 0);

			switch (msg) {
			case MESSAGE_TYPE_ROOM_ADD: {
				Room new_room;
				error_msg = recv(other_socket, reinterpret_cast<char*>(&new_room), sizeof(Room), 0);
				if (error_msg == SOCKET_ERROR) {

				}
				new_room.host_socket = other_socket;
				rooms.push_back(new_room);

			} break;

			case MESSAGE_TYPE_ROOM_JOIN: {
				u32 room_to_join;
				error_msg = recv(other_socket, reinterpret_cast<char*>(&room_to_join), sizeof(u32), 0);
				if (error_msg == SOCKET_ERROR) {

				}
				if (room_to_join < rooms.size() && rooms[room_to_join].current_pads < rooms[room_to_join].max_pads) {
					u32 client_slot = rooms[room_to_join].current_pads++;
					send(rooms[room_to_join].host_socket, "AAAA", sizeof("AAAA"), 0);
					send(other_socket, reinterpret_cast<char*>(&client_slot), sizeof(u32), 0);
				}
			} break;

			case MESSAGE_TYPE_ROOM_QUERY: {
				u32 room_count = rooms.size();
				error_msg = send(other_socket, reinterpret_cast<char*>(&room_count), sizeof(u32), 0);
				if (error_msg == SOCKET_ERROR) {

				}
				for (u32 i = 0; i < rooms.size(); i++) {
					send(other_socket, reinterpret_cast<char*>(&rooms[i]), sizeof(Room), 0);
				}
			} break;

			case MESSAGE_TYPE_SEND_PAD_DATA: {
				u32 chosen_room, client_slot;
				XINPUT_STATE pad_state;
				recv(other_socket, reinterpret_cast<char*>(&chosen_room), sizeof(u32), 0);
				recv(other_socket, reinterpret_cast<char*>(&client_slot), sizeof(u32), 0);
				recv(other_socket, reinterpret_cast<char*>(&pad_state.Gamepad), sizeof(XINPUT_GAMEPAD), 0);

				if (chosen_room >= rooms.size()) {
					//TODO
				}

				PadSignal pad_signal;
				pad_signal.pad_number = client_slot;
				pad_signal.pad_state = pad_state;
				send(rooms[chosen_room].host_socket, reinterpret_cast<char*>(&pad_signal), sizeof(PadSignal), 0);
			}break;

			}
		}
	};

	while (true) {
		SOCKET new_host = accept(server_socket, nullptr, nullptr);
		host_threads.emplace_back(handle_connection, new_host);
	}

	for (auto& thd : host_threads)
		if (thd.joinable())
			thd.join();
}
