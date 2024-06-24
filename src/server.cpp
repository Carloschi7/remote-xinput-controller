#include "server.hpp"
#include <vector>
#include <iostream>



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

	struct {
		SOCKET socket_requested = 0;
		Message connecting_message;
		std::mutex notify_mutex;
		std::condition_variable notify_cv;
	} notify_data;

	//TODO add a rooms mutex
	auto handle_connection = [&](SOCKET other_socket) {
		bool is_this_client_hosting = false;

		while (true) {
			Message msg;
			u32 error_msg = recv(other_socket, reinterpret_cast<char*>(&msg), sizeof(Message), 0);

			if (error_msg == SOCKET_ERROR) {

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
						rooms.erase(rooms.begin() + socket_room);
					}
				}
				else {
					for (u32 i = 0; i < rooms.size(); i++) {
						for (u32 j = 0; j < 4; j++) {
							auto& sock_info = rooms[i].connected_sockets[j];
							if (other_socket == sock_info.sock) {
								sock_info.slot_taken = false;
								rooms[i].current_pads--;
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

				Room new_room;
				error_msg = recv(other_socket, reinterpret_cast<char*>(&new_room), sizeof(Room), 0);
				if (error_msg == SOCKET_ERROR) {

				}
				new_room.host_socket = other_socket;
				rooms.push_back(new_room);

			} break;

			case MESSAGE_REQUEST_ROOM_JOIN: {
				is_this_client_hosting = false;

				u32 room_to_join;
				error_msg = recv(other_socket, reinterpret_cast<char*>(&room_to_join), sizeof(u32), 0);
				if (error_msg == SOCKET_ERROR) {

				}
				if (room_to_join < rooms.size() && rooms[room_to_join].current_pads < rooms[room_to_join].max_pads) {
					rooms[room_to_join].current_pads++;

					Message host_msg = MESSAGE_INFO_CLIENT_JOINING_ROOM;
					send(rooms[room_to_join].host_socket, reinterpret_cast<char*>(&host_msg), sizeof(Message), 0);

					{
						//Ask for the host thread if there are any issues during the connecting phase
						std::unique_lock lk{ notify_data.notify_mutex };
						if (notify_data.socket_requested == 0) {
							notify_data.socket_requested = rooms[room_to_join].host_socket;
							notify_data.notify_cv.wait(lk);
							host_msg = notify_data.connecting_message;
							notify_data.socket_requested = 0;
						}
					}

					Message response;
					if (host_msg == MESSAGE_ERROR_NONE) {

						u32 client_slot = 0;
						while (client_slot < XUSER_MAX_COUNT && rooms[room_to_join].connected_sockets[client_slot].slot_taken)
							client_slot++;

						rooms[room_to_join].connected_sockets[client_slot].sock = other_socket;
						rooms[room_to_join].connected_sockets[client_slot].slot_taken = true;

						response = MESSAGE_ERROR_NONE;
					}
					else {
						response = MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD;
					}
					send(other_socket, reinterpret_cast<char*>(&response), sizeof(Message), 0);
				}
				else {
					Message response = MESSAGE_ERROR_ROOM_AT_FULL_CAPACITY;
					send(other_socket, reinterpret_cast<char*>(&response), sizeof(Message), 0);
				}
			} break;

			case MESSAGE_REQUEST_ROOM_QUERY: {
				u32 room_count = rooms.size();
				error_msg = send(other_socket, reinterpret_cast<char*>(&room_count), sizeof(u32), 0);
				if (error_msg == SOCKET_ERROR) {

				}
				for (u32 i = 0; i < rooms.size(); i++) {
					send(other_socket, reinterpret_cast<char*>(&rooms[i]), sizeof(Room), 0);
				}
			} break;

			case MESSAGE_REQUEST_SEND_PAD_DATA: {
				u32 chosen_room, client_slot;
				XINPUT_GAMEPAD pad_state;
				recv(other_socket, reinterpret_cast<char*>(&chosen_room), sizeof(u32), 0);
				recv(other_socket, reinterpret_cast<char*>(&pad_state), sizeof(XINPUT_GAMEPAD), 0);

				if (chosen_room < rooms.size()) {
					bool found_match = false;
					for (u32 i = 0; i < 4; i++) {
						if (rooms[chosen_room].connected_sockets[i].sock == other_socket) {
							found_match = true;
							client_slot = i;
						}
					}

					if (found_match) {
						PadSignal pad_signal;
						pad_signal.pad_number = client_slot;
						pad_signal.pad_state = pad_state;
						send(rooms[chosen_room].host_socket, reinterpret_cast<char*>(&pad_signal), sizeof(PadSignal), 0);
					}
				}


			}break;
			case MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD:
			case MESSAGE_INFO_PAD_ALLOCATED: {
				if (is_this_client_hosting) {
					std::unique_lock lk{ notify_data.notify_mutex };
					if (notify_data.socket_requested == other_socket) {
						notify_data.connecting_message = msg == MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD ? msg : MESSAGE_ERROR_NONE;
						notify_data.notify_cv.notify_one();
					}
				}
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
