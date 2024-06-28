#include "server.hpp"
#include <iostream>


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

void StartServer()
{
	SOCKET server_socket = SetupServerSocket(20000);

	std::vector<std::thread> host_threads;
	ServerData server_data;

	while (true) {
		SOCKET new_host = accept(server_socket, nullptr, nullptr);
		host_threads.emplace_back(&HandleConnection, &server_data, new_host);
	}

	for (auto& thd : host_threads)
		if (thd.joinable())
			thd.join();
}

void HandleConnection(ServerData* server_data, SOCKET other_socket)
{
	bool is_this_client_hosting = false;
	auto& rooms = server_data->rooms;
	auto& rooms_mutex = server_data->rooms_mutex;

	while (true) {
		//We actually care about the possibility of errors here
		Message msg;
		bool correct_recv = Receive(other_socket, &msg);
		std::unique_lock rooms_lock{ rooms_mutex };

		if (!correct_recv) {

			if (is_this_client_hosting) {
				s32 socket_room = -1;
				for (u32 i = 0; i < rooms.size(); i++) {
					if (other_socket == rooms[i].host_socket) {
						socket_room = static_cast<s32>(i);
						break;
					}
				}

				//Should always be true
				if (socket_room != -1) {

					delete rooms[socket_room].mtx;
					delete rooms[socket_room].notify_cv;
					rooms.erase(rooms.begin() + socket_room);
				}
			}
			else {
				for (u32 i = 0; i < rooms.size(); i++) {
					for (u32 j = 0; j < 4; j++) {
						auto& sock_info = rooms[i].connected_sockets[j];
						if (other_socket == sock_info.sock) {
							sock_info.slot_taken = false;
							rooms[i].info.current_pads--;
							break;
						}
					}

				}
			}

			break;
		}

		switch (msg) {
		case MESSAGE_REQUEST_ROOM_CREATE: {
			is_this_client_hosting = true;

			Room::Info new_room_info;
			Receive(other_socket, &new_room_info);
			Room& room = rooms.emplace_back();
			room.info = new_room_info;
			room.host_socket = other_socket;
			room.mtx = new std::mutex;
			room.notify_cv = new std::condition_variable;

		} break;

		case MESSAGE_REQUEST_ROOM_JOIN: {
			is_this_client_hosting = false;

			u32 room_to_join;
			Receive(other_socket, &room_to_join);

			if (room_to_join >= rooms.size()) {
				//TODO need a new message
				break;
			}

			Room& room = rooms[room_to_join];
			if (room.info.current_pads < room.info.max_pads) {
				room.info.current_pads++;

				SendMsg(rooms[room_to_join].host_socket, MESSAGE_INFO_CLIENT_JOINING_ROOM);

				Message host_msg;
				{
					//Ask for the host thread if there are any issues during the connecting phase
					std::unique_lock lk{ *room.mtx };
					//Always unlock the room lock to allow the host thread to send the connecting message
					rooms_lock.unlock();
					room.notify_cv->wait(lk);
					rooms_lock.lock();
					host_msg = room.connecting_message;
				}

				Message response;
				if (host_msg == MESSAGE_ERROR_NONE) {

					u32 client_slot = 0;
					while (client_slot < XUSER_MAX_COUNT && rooms[room_to_join].connected_sockets[client_slot].slot_taken)
						client_slot++;

					rooms[room_to_join].connected_sockets[client_slot].sock = other_socket;
					rooms[room_to_join].connected_sockets[client_slot].slot_taken = true;

					SendMsg(other_socket, MESSAGE_ERROR_NONE);
				}
				else {
					SendMsg(other_socket, MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD);
				}
			}
			else {
				SendMsg(other_socket, MESSAGE_ERROR_ROOM_AT_FULL_CAPACITY);
			}
		} break;

		case MESSAGE_REQUEST_ROOM_QUERY: {
			u32 room_count = rooms.size();
			Send(other_socket, room_count);
			for (u32 i = 0; i < rooms.size(); i++) {
				Send(other_socket, rooms[i].info);
			}
		} break;

		case MESSAGE_REQUEST_SEND_PAD_DATA: {
			u32 chosen_room, client_slot;
			XINPUT_GAMEPAD pad_state;
			Receive(other_socket, &chosen_room);
			Receive(other_socket, &pad_state);

			bool found_match = false;
			if (chosen_room < rooms.size()) {
				for (u32 i = 0; i < 4; i++) {
					if (rooms[chosen_room].connected_sockets[i].sock == other_socket) {
						found_match = true;
						client_slot = i;
						break;
					}
				}

				//The socket is actually connected to the room
				if (found_match) {
					PadSignal pad_signal;
					pad_signal.pad_number = client_slot;
					pad_signal.pad_state = pad_state;
					SendMsg(other_socket, MESSAGE_ERROR_NONE);
					Send(rooms[chosen_room].host_socket, pad_signal);
				}
			}

			if (!found_match) {
				//Probably the client is looking for an old room that
				//was deleted and does not exist anymore, notify the client
				SendMsg(other_socket, MESSAGE_ERROR_ROOM_NO_LONGER_EXISTS);
			}


		}break;
		case MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD:
		case MESSAGE_INFO_PAD_ALLOCATED: {
			if (is_this_client_hosting) {
				u32 room_index = 0;
				for (u32 i = 0; i < rooms.size(); i++) {
					if (rooms[i].host_socket == other_socket) {
						room_index = i;
						break;
					}
				}

				std::unique_lock lk{ *rooms[room_index].mtx };
				rooms[room_index].connecting_message = msg == MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD ? msg : MESSAGE_ERROR_NONE;
				rooms[room_index].notify_cv->notify_one();
			}
		}break;
		}
	}
}
